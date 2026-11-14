'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const E=path.join(__dirname,'运行核验/31号实施_20261101');
const json=n=>JSON.parse(fs.readFileSync(path.join(E,n),'utf8').replace(/^\uFEFF/,''));
const model=json('31号实际工程读回.json'),resources=json('资源索引绑定.json');
function initialize(c){
 for(const p of model.Pages){if(!c[p.Name])c[p.Name]={id:p.Id};
  for(const control of p.Controls){const a=control.Properties,o=c[p.Name][control.Name]||(c[p.Name][control.Name]={});
   for(const [k,v] of Object.entries({x:a.X,y:a.Y,width:a.Width,height:a.Height,txt:a.Text,fontColor:a.FontColor,bgColor:a.BackgroundColor,enable:a.Enable,visible:a.Visbile,val:a.Int??a.Value,font:model.Fonts.findIndex(f=>f.Id===a.FontId)+1}))if(o[k]===undefined&&v!==undefined)o[k]=v;
  }
 }
 return c;
}
function compile(s){return new vm.Script(s.replace(/^\uFEFF/,'').replace(/^int (\w+);$/gm,'var $1;').replace(/\(byte\)/g,'').replace(/user\.(image|font)\.[A-Za-z0-9_]+/g,k=>resources[k]).replace('ui31Milli=ui31Abs*13+(ui31Abs*768794+5000000)/10000000;','ui31Milli=ui31Abs*13+Math.floor((ui31Abs*768794+5000000)/10000000);').replace('ui31Whole=ui31Milli/1000;','ui31Whole=Math.floor(ui31Milli/1000);').replace(/ui31Thumb=24\+ui31Slider\*944\/3200;/g,'ui31Thumb=24+Math.floor(ui31Slider*944/3200);'));}
module.exports={initialize,compile,model,resources,json,E};
