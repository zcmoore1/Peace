<#
mw2_aimdump.ps1 -- read MW2's game pad aim data out of a running IW4x.

Peace's game pad aim (code/client/cl_gamepadaim.c) still lacks two things
that live only in MW2 itself: the aim input graphs (aaInputGraph, the curve
for partial stick deflection) and the code that sets fovTurnRateScale (the
turn-rate scale for a narrower field of view). This reads both out of your
own running game so they can be ported from MW2's own data and code.

Run it while IW4x is in a match (a private match is fine: the aim assist
data is set up when a map loads), from any PowerShell window:

  powershell -ExecutionPolicy Bypass -File dev\tools\mw2_aimdump.ps1

It only reads the game's memory; it changes nothing. The report is printed,
saved to %TEMP%\mw2_aimdump.txt and put on the clipboard. Do not run it on a
public server.

The addresses are IW4x's names for iw4mp.exe (iw4x-client
src/Game/Functions.cpp, Structs.hpp): aaInputGraph 0x7A2FC0 (4 GraphFloat,
328 bytes each), aaGlobArray 0x7A2110 (AimAssistGlobals: fWeaponPosFrac +32,
initialized +176, fovTurnRateScale +288, fovScaleInv +292, adsLerp +296),
AimAssist_UpdateTweakables 0x569950, AimAssist_UpdateAdsLerp 0x569AA0.
#>
param([string]$ProcessName = "")
$ErrorActionPreference = "Stop"

# C# 5 only: Windows PowerShell 5.1 compiles this with the .NET Framework's csc.
$source = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

public delegate bool Mw2MemReader(long address, byte[] buffer);

public static class Mw2AimDump
{
	const long ImageBase = 0x400000;
	const long AimGlobals = 0x7A2110;
	const long InputGraphs = 0x7A2FC0;
	const int GraphSize = 328;
	const long CodeStart = 0x569950;	// AimAssist_UpdateTweakables
	const int CodeSize = 0x3B0;			// through AimAssist_UpdateAdsLerp (0x569AA0) and past it

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr OpenProcess(int access, bool inherit, int pid);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, IntPtr size, out IntPtr read);
	[DllImport("kernel32.dll")]
	static extern bool CloseHandle(IntPtr handle);

	static IntPtr process;

	static bool ReadProcess(long address, byte[] buffer)
	{
		IntPtr read;
		return ReadProcessMemory(process, new IntPtr(address), buffer, new IntPtr(buffer.Length), out read) &&
			read.ToInt64() == buffer.Length;
	}

	public static string Run(int pid)
	{
		process = OpenProcess(0x0010 | 0x0400, false, pid);	// PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
		if (process == IntPtr.Zero)
			return "ERROR: could not open the game (Windows error " + Marshal.GetLastWin32Error() +
				"). If IW4x runs as administrator, run PowerShell as administrator too.";
		try { return Report(ReadProcess); }
		finally { CloseHandle(process); }
	}

	static string F(float v) { return v.ToString("R", CultureInfo.InvariantCulture); }

	static byte[] Read(Mw2MemReader read, long address, int size)
	{
		byte[] b = new byte[size];
		return read(address, b) ? b : null;
	}

	static void Hex(StringBuilder sb, long address, byte[] b)
	{
		for (int i = 0; i < b.Length; i += 32)
		{
			sb.AppendFormat("{0:X8}:", address + i);
			for (int j = i; j < Math.Min(i + 32, b.Length); j++)
				sb.AppendFormat(" {0:X2}", b[j]);
			sb.AppendLine();
		}
	}

	static string Str(Mw2MemReader read, long address)
	{
		byte[] b = Read(read, address, 48);
		if (b == null) return null;
		int n = 0;
		while (n < b.Length && b[n] != 0)
		{
			if (b[n] < 0x20 || b[n] > 0x7E) return null;
			n++;
		}
		return n >= 2 && n < b.Length ? Encoding.ASCII.GetString(b, 0, n) : null;
	}

	public static string Report(Mw2MemReader read)
	{
		StringBuilder sb = new StringBuilder();
		sb.AppendLine("== mw2_aimdump 1 ==");

		byte[] dos = Read(read, ImageBase, 0x40);
		if (dos == null || dos[0] != 'M' || dos[1] != 'Z')
			return sb.Append("ERROR: no executable at 0x400000; is this IW4x?").AppendLine().ToString();
		int peOffset = BitConverter.ToInt32(dos, 0x3C);
		byte[] nt = Read(read, ImageBase + peOffset, 0x60);
		if (nt == null || nt[0] != 'P' || nt[1] != 'E')
			return sb.Append("ERROR: bad PE header").AppendLine().ToString();
		uint stamp = BitConverter.ToUInt32(nt, 8);
		uint sizeOfCode = BitConverter.ToUInt32(nt, 28);
		uint baseOfCode = BitConverter.ToUInt32(nt, 44);
		uint sizeOfImage = BitConverter.ToUInt32(nt, 80);
		long imageEnd = ImageBase + sizeOfImage;
		sb.AppendFormat("image stamp {0:X8} size {1:X} code {2:X}+{3:X}", stamp, sizeOfImage, baseOfCode, sizeOfCode).AppendLine();

		// The aim input graphs, as GraphFloat: name[64], knots[32][2], knotCount, scale.
		for (int g = 0; g < 4; g++)
		{
			long a = InputGraphs + (long)GraphSize * g;
			byte[] b = Read(read, a, GraphSize);
			if (b == null) { sb.AppendFormat("graph {0}: unreadable", g).AppendLine(); continue; }
			string name = Encoding.ASCII.GetString(b, 0, 64);
			int z = name.IndexOf('\0');
			if (z >= 0) name = name.Substring(0, z);
			int count = BitConverter.ToUInt16(b, 320);
			float scale = BitConverter.ToSingle(b, 324);
			sb.AppendFormat("graph {0} \"{1}\" knots {2} scale {3}", g, name, count, F(scale)).AppendLine();
			StringBuilder k = new StringBuilder();
			for (int i = 0; i < Math.Min(count, 32); i++)
			{
				if (i > 0) k.Append(' ');
				k.Append(F(BitConverter.ToSingle(b, 64 + 8 * i))).Append(' ').Append(F(BitConverter.ToSingle(b, 68 + 8 * i)));
			}
			sb.AppendFormat("  seta aim_input_graph \"{0}\"; seta aim_input_graph_scale {1}", k, F(scale)).AppendLine();
		}

		byte[] aa = Read(read, AimGlobals, 0x130);
		if (aa != null)
			sb.AppendFormat("aaGlob initialized {0} fWeaponPosFrac {1} fovTurnRateScale {2} fovScaleInv {3} adsLerp {4}",
				aa[176], F(BitConverter.ToSingle(aa, 32)), F(BitConverter.ToSingle(aa, 288)),
				F(BitConverter.ToSingle(aa, 292)), F(BitConverter.ToSingle(aa, 296))).AppendLine();

		// The code that sets the aim assist's per-frame state.
		byte[] code = Read(read, CodeStart, CodeSize);
		if (code != null)
		{
			sb.AppendFormat("code {0:X8}", CodeStart).AppendLine();
			Hex(sb, CodeStart, code);

			// Every address in the image the code names: its dword and float,
			// and the dvar or string it leads to (dvar_t: name +0, current +16).
			SortedDictionary<uint, bool> seen = new SortedDictionary<uint, bool>();
			for (int i = 0; i + 4 <= code.Length; i++)
			{
				uint v = BitConverter.ToUInt32(code, i);
				if (v >= ImageBase && v < imageEnd) seen[v] = true;
			}
			sb.AppendLine("refs");
			foreach (uint v in seen.Keys)
			{
				byte[] d = Read(read, v, 4);
				if (d == null) continue;
				uint dw = BitConverter.ToUInt32(d, 0);
				sb.AppendFormat("  {0:X8}: {1:X8} {2}", v, dw, F(BitConverter.ToSingle(d, 0)));
				if (dw >= ImageBase && dw < imageEnd)
				{
					byte[] p = Read(read, dw, 4);
					string s = p == null ? null : Str(read, BitConverter.ToUInt32(p, 0));
					if (s != null)
					{
						byte[] cur = Read(read, dw + 16, 16);
						sb.AppendFormat(" dvar \"{0}\"", s);
						if (cur != null)
							sb.AppendFormat(" current {0} {1:X8}", F(BitConverter.ToSingle(cur, 0)), BitConverter.ToUInt32(cur, 0));
					}
					else
					{
						s = Str(read, dw);
						if (s != null) sb.AppendFormat(" string \"{0}\"", s);
					}
				}
				sb.AppendLine();
			}
		}

		// Everywhere the game's code names these fields, with the bytes around it.
		long[] fields = { AimGlobals + 288, AimGlobals + 292, AimGlobals + 296, AimGlobals + 32, InputGraphs };
		string[] names = { "fovTurnRateScale", "fovScaleInv", "adsLerp", "fWeaponPosFrac", "aaInputGraph" };
		// Read the code in 64KB pieces, so one unreadable page costs only its piece.
		byte[] text = new byte[sizeOfCode];
		for (long at = 0; at < sizeOfCode; at += 0x10000)
		{
			int n = (int)Math.Min(0x10000, sizeOfCode - at);
			byte[] piece = Read(read, ImageBase + baseOfCode + at, n);
			if (piece != null) Array.Copy(piece, 0, text, at, n);
			else sb.AppendFormat("unreadable code at {0:X8}+{1:X}", ImageBase + baseOfCode + at, n).AppendLine();
		}
		{
			List<long> bases = new List<long>();
			for (int i = 0; i + 4 <= text.Length; i++)
			{
				uint v = BitConverter.ToUInt32(text, i);
				if (v == AimGlobals && bases.Count < 64) bases.Add(ImageBase + baseOfCode + i);
				for (int f = 0; f < fields.Length; f++)
				{
					if (v != fields[f]) continue;
					long at = ImageBase + baseOfCode + i;
					int from = Math.Max(0, i - 64), to = Math.Min(text.Length, i + 68);
					byte[] around = new byte[to - from];
					Array.Copy(text, from, around, 0, around.Length);
					sb.AppendFormat("xref {0} at {1:X8}", names[f], at).AppendLine();
					Hex(sb, ImageBase + baseOfCode + from, around);
				}
			}
			sb.Append("aaGlobArray named at");
			foreach (long b in bases) sb.AppendFormat(" {0:X8}", b);
			sb.AppendLine();
		}

		sb.AppendLine("== end ==");
		return sb.ToString();
	}
}
'@
if (-not ([System.Management.Automation.PSTypeName]'Mw2AimDump').Type) {
	Add-Type -TypeDefinition $source -Language CSharp
}

$names = if ($ProcessName) { @($ProcessName) } else { @("iw4x", "iw4mp") }
$game = $null
foreach ($name in $names) {
	$game = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($game) { break }
}
if (-not $game) {
	Write-Host "IW4x is not running. Start it, load a private match, then run this again."
	exit 1
}

$report = [Mw2AimDump]::Run($game.Id)
$path = Join-Path $env:TEMP "mw2_aimdump.txt"
Set-Content -Path $path -Value $report -Encoding ASCII
$clip = ""
try { Set-Clipboard -Value $report; $clip = " It is on your clipboard too." } catch { }
Write-Host $report
Write-Host "Saved to $path.$clip Paste it back to Claude."
