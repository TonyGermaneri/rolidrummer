// Keep the artwork and its HTML controls in the same undistorted coordinate box.
export function fitSeaboard(width:number,height:number){
 const w=Math.max(0,Math.min(width,height*705/210))
 return {width:w,height:w*210/705}
}
