'use strict';
const {REGIONS,COMMAND,frame,validFrame,wordsToBytes,bytesToWords,put32,get32}=require('./r2-protocol.cjs');
class Simulator {
    constructor({axis=3,boot=0x12345678,control=true}={}) {
        this.axis=axis;this.boot=boot;this.control=control;this.now=0;this.session=0;this.owner=0;this.lastSequence=0;this.target=0;this.actual=0;this.result=Array(20).fill(0);this.stop=null;this.stopEvent=0;this.stopRecord=null;this.lastWords=null;this.acceptedResult=null;this.lastTarget=0;this.lastLease=0;
    }
    tick(milliseconds) {
        this.now+=milliseconds;
        if(this.session && !this.stop && (this.now-this.lastTarget>=500 || this.now-this.lastLease>=1000)) this.beginStop(3,this.lastSequence);
        if(this.stop){
            const step=this.stop.decel*275.295/1000*milliseconds/1000;
            this.actual=Math.sign(this.actual)*Math.max(0,Math.abs(this.actual)-step);
            if(this.actual===0 && this.now-this.stop.started>=200){this.stopRecord={...this.stop,result:3,completed:this.now};this.publish(this.stop.command,this.stop.sequence,3,0);this.session=0;this.owner=0;this.stop=null;}
        }else{
            const step=41.29425*milliseconds/1000;
            const difference=this.target-this.actual;
            if(Math.sign(this.target)!==Math.sign(this.actual) && this.actual!==0 && this.target!==0) this.actual=Math.sign(this.actual)*Math.max(0,Math.abs(this.actual)-step);
            else this.actual+=Math.sign(difference)*Math.min(Math.abs(difference),step);
        }
    }
    publish(command,sequence,state,error) {
        const words=Array(20).fill(0);put32(words,0,this.boot,true);put32(words,2,this.session,true);put32(words,4,sequence,true);words[6]=command;words[7]=state;words[8]=error;words[9]=1;put32(words,10,this.now,true);if(state>=3)put32(words,12,this.now,true);words[16]=this.owner;words[18]=this.actual===0?this.axis:0;this.result=words;
    }
    beginStop(command,sequence) {
        if(!this.stop){this.stop={event:++this.stopEvent,started:this.now,sequence,command,decel:command===16?500:400,result:2,owner:this.owner};}
        else if(command===16){this.stop.command=16;this.stop.sequence=sequence;this.stop.decel=500;}
        this.target=0;this.publish(command,sequence,this.owner?2:3,0);
        if(!this.owner){this.stopRecord={...this.stop,result:3,completed:this.now};this.stop=null;}
    }
    submit(words) {
        const command=words[8],sequence=get32(words,6),session=get32(words,4),rpm=get32(words,10)|0;
        let error=0;
        if(words.length!==24 || words[0]!==0x484D || words[1]!==1 || words[22]!==2 || words[23]!==0xA55A || words[9]!==0 || [12,13,14,15,16,18,19,20,21].some(index=>words[index]!==0))error=9;
        else if(get32(words,2)!==this.boot)error=2;
        else if(!this.control)error=1;
        else if(![1,2,3,16,33].includes(command))error=15;
        else if(command===33 && (rpm< -1600 || rpm>1600 || words[17]<1 || words[17]>500))error=8;
        else if(command!==33 && (rpm!==0 || words[17]!==0))error=9;
        if(!error && sequence===this.lastSequence && JSON.stringify(words)===JSON.stringify(this.lastWords)){this.result=[...this.acceptedResult];return;}
        if(!error && (!sequence || sequence<=this.lastSequence))error=sequence===this.lastSequence?10:11;
        if(!error && command===1){if(session)error=3;else if(this.owner)error=5;else{this.owner=1;this.session=9;this.target=0;this.lastTarget=this.lastLease=this.now;}}
        else if(!error && command===16 && session===0)this.beginStop(command,sequence);
        else if(!error){
            if(!this.session || session!==this.session)error=3;
            else if(command===16)this.beginStop(command,sequence);
            else if(this.stop)error=4;
            else if(command===3)this.beginStop(command,sequence);
            else if(command===33){this.target=rpm;this.lastTarget=this.lastLease=this.now;}
            else if(command===2)this.lastLease=this.now;
        }
        if(error){this.publish(command,sequence,4,error);return;}
        this.lastSequence=sequence;this.lastWords=[...words];
        if(command!==3 && command!==16)this.publish(command,sequence,command===33?1:3,0);
        this.acceptedResult=[...this.result];
    }
    registers() {
        const registers=Array(0x4A0).fill(0);const status=Array(64).fill(0);
        registers[192]=0x5249;registers[193]=1;registers[194]=7;registers[28]=1;
        const meta=Array(24).fill(0);meta[0]=0x5249;meta[1]=1;meta[2]=this.control?255:1;meta[3]=2;put32(meta,4,this.boot);put32(meta,6,Math.floor(this.now/100));put32(meta,8,this.now);meta[10]=100;put32(meta,14,this.session);meta[16]=this.owner;meta[20]=this.axis;meta[21]=5811;meta[22]=1000;
        status[0]=0x534D;status[1]=2;put32(status,2,this.boot);put32(status,4,Math.floor(this.now/100));put32(status,6,this.now);status[8]=this.owner;status[9]=this.axis;status[10]=this.stop?4:this.session?this.target?3:2:0;status[11]=129+(this.session&&!this.stop?2:0)+(this.stop?4:0);put32(status,12,this.session);status[14]=Math.max(0,1000-(this.now-this.lastLease));status[15]=Math.min(65535,this.now-this.lastTarget);put32(status,16,this.lastSequence);status[18]=this.target&65535;
        for(let axis=0;axis<2;axis++){const bit=1<<axis;status[19+axis]=this.axis&bit?this.target&65535:32767;status[21+axis]=this.axis&bit?Math.round(this.actual)&65535:32767;status[23+axis]=status[21+axis];status[46+axis]=this.axis&bit?0:65535;}
        status[25]=status[26]=status[27]=status[57]=this.axis;status[28]=this.actual===0?this.axis:0;status[38]=(-1600)&65535;status[39]=1600;status[40]=150;status[41]=300;status[42]=400;status[43]=500;status[44]=500;status[45]=1000;
        const stop=this.stop||this.stopRecord;if(stop){status[30]=this.stop?this.actual?1:2:4;status[31]=stop.decel;put32(status,32,this.now-stop.started);put32(status,34,18000);put32(status,48,stop.event);put32(status,50,stop.sequence);status[52]=stop.command;status[53]=stop.result;put32(status,54,stop.completed||0);status[56]=stop.owner;}
        registers.splice(512,24,...meta);registers.splice(288,20,...this.result);registers.splice(1120,64,...status);return registers;
    }
    handle(data) {
        if(!validFrame(data)||data[0]!==1)return null;
        const code=data[1];if(code===3){if(data.length!==8)return frame([1,0x83,3]);const start=data.readUInt16BE(2),count=data.readUInt16BE(4);if(!REGIONS.some(region=>region[0]===start&&region[1]===count))return frame([1,0x83,2]);return frame(Buffer.concat([Buffer.from([1,3,count*2]),wordsToBytes(this.registers().slice(start,start+count))]));}
        if(code===16){if(data.length!==57||data.readUInt16BE(2)!==256||data.readUInt16BE(4)!==24||data[6]!==48)return frame([1,0x90,3]);this.submit(bytesToWords(data.subarray(7,-2)));return frame([1,16,1,0,0,24]);}
        return frame([1,code|128,1]);
    }
}
module.exports={Simulator};
if(require.main===module){
    const net=require('node:net');const port=Number(process.argv[2]||15013);const sim=new Simulator();
    net.createServer(socket=>{let pending=Buffer.alloc(0);socket.on('error',error=>console.error('模拟连接断开：'+error.code));socket.on('data',chunk=>{pending=Buffer.concat([pending,chunk]);while(pending.length>=8){const length=pending[1]===16?pending.length>=7?9+pending[6]:Infinity:8;if(pending.length<length)break;const request=pending.subarray(0,length);pending=pending.subarray(length);const response=sim.handle(request);if(response)socket.write(response);}});}).listen(port,'127.0.0.1',()=>console.log('R2模拟从站仅监听本机TCP '+port+'，不接物理电机；RTU字节透传。'));
    setInterval(()=>sim.tick(10),10);
}
