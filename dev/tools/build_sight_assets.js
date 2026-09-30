/* Original Peace reflex sight assets. No third-party models or textures.
 * Run: node dev/tools/build_sight_assets.js
 * Deterministic MD3 + uncompressed TGA, requiring only Node's standard library.
 */
function buildSightAssets() {
    const vertices = [], normals = [], triangles = [];
    function quad(a, b, c, d) {
        const u = b.map((v, i) => v - a[i]), v = c.map((v, i) => v - a[i]);
        const n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]];
        const length = Math.hypot(...n), first = vertices.length;
        vertices.push(a, b, c, d);
        for (let i = 0; i < 4; i++) normals.push(n.map(x => x / length));
        triangles.push([first, first+1, first+2], [first, first+2, first+3]);
    }
    const point = (x, r, t) => [x, r*Math.cos(t), r*Math.sin(t)];
    for (let i = 0; i < 24; i++) {
        const a = i*Math.PI/12, b = (i+1)*Math.PI/12;
        quad(point(-.5,1.05,a), point(-.5,1.05,b), point(.5,1.05,b), point(.5,1.05,a));
        quad(point(-.5,.8,b), point(-.5,.8,a), point(.5,.8,a), point(.5,.8,b));
        quad(point(-.5,.8,a), point(-.5,.8,b), point(-.5,1.05,b), point(-.5,1.05,a));
        quad(point(.5,.8,b), point(.5,.8,a), point(.5,1.05,a), point(.5,1.05,b));
    }
    const box = [
        [-.65,-.5,-1.95], [.65,-.5,-1.95], [.65,.5,-1.95], [-.65,.5,-1.95],
        [-.65,-.5,-.85], [.65,-.5,-.85], [.65,.5,-.85], [-.65,.5,-.85]
    ];
    for (const face of [[0,3,2,1],[4,5,6,7],[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7]]) {
        quad(...face.map(i => box[i]));
    }
    const frame = 108, surface = frame + 56;
    const ofsTriangles = 108, ofsShaders = ofsTriangles + triangles.length*12;
    const ofsSt = ofsShaders + 68, ofsXyz = ofsSt + vertices.length*8;
    const surfaceEnd = ofsXyz + vertices.length*8;
    const md3 = new Uint8Array(surface + surfaceEnd), view = new DataView(md3.buffer);
    const int = (o,v) => view.setInt32(o,v,true);
    const float = (o,v) => view.setFloat32(o,v,true);
    function text(o,s) { for (let i=0;i<s.length;i++) md3[o+i]=s.charCodeAt(i); }
    const magic = 0x33504449;
    int(0,magic); int(4,15); text(8,"Peace reflex");
    [0,1,0,1,0,frame,surface,surface,md3.length].forEach((v,i)=>int(72+i*4,v));
    for (let i=0;i<3;i++) {
        float(frame+i*4,Math.min(...vertices.map(v=>v[i])));
        float(frame+12+i*4,Math.max(...vertices.map(v=>v[i])));
    }
    float(frame+36,2.4); text(frame+40,"idle");
    int(surface,magic); text(surface+4,"housing");
    [0,1,1,vertices.length,triangles.length,ofsTriangles,ofsShaders,ofsSt,ofsXyz,surfaceEnd]
        .forEach((v,i)=>int(surface+68+i*4,v));
    triangles.forEach((t,i)=>t.forEach((v,j)=>int(surface+ofsTriangles+i*12+j*4,v)));
    text(surface+ofsShaders,"peace/optic_housing");
    vertices.forEach((v,i)=>{
        float(surface+ofsSt+i*8,(i%4===1||i%4===2)?1:0);
        float(surface+ofsSt+i*8+4,i%4>=2?1:0);
        v.forEach((x,j)=>view.setInt16(surface+ofsXyz+i*8+j*2,Math.round(x*64),true));
        const n=normals[i], azimuth=Math.round(Math.atan2(n[1],n[0])*255/(2*Math.PI))&255;
        const polar=Math.round(Math.acos(Math.max(-1,Math.min(1,n[2])))*255/(2*Math.PI))&255;
        view.setUint16(surface+ofsXyz+i*8+6,(azimuth<<8)|polar,true);
    });
    function tga(size,pixel) {
        const bytes=new Uint8Array(18+size*size*4), data=new DataView(bytes.buffer);
        bytes[2]=2; data.setUint16(12,size,true); data.setUint16(14,size,true);
        bytes[16]=32; bytes[17]=0x28;
        for(let y=0;y<size;y++)for(let x=0;x<size;x++){
            const [r,g,b,a]=pixel(x,y), p=18+(y*size+x)*4;
            bytes.set([b,g,r,a],p);
        }
        return bytes;
    }
    return {
        "assets/baseq3/models/optics/peace_reflex.md3":md3,
        "assets/baseq3/gfx/peace/optic_housing.tga":tga(2,()=>[64,67,71,255]),
        "assets/baseq3/gfx/peace/optic_dot.tga":tga(32,(x,y)=>{
            const r=Math.hypot((x-15.5)/15.5,(y-15.5)/15.5);
            return [255,255,255,Math.round(255*Math.max(0,Math.min(1,(1-r)*6)))];
        })
    };
}
if (typeof module !== "undefined") {
    module.exports = {buildSightAssets};
    if (require.main === module) {
        const fs=require("node:fs"), path=require("node:path");
        const root=path.resolve(__dirname,"../..");
        for(const [name,data] of Object.entries(buildSightAssets())) {
            const file=path.join(root,name);
            fs.mkdirSync(path.dirname(file),{recursive:true});
            fs.writeFileSync(file,data);
            console.log(name+" ("+data.length+" bytes)");
        }
    }
}
