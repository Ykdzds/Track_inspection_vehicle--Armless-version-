'use strict';
const REGIONS = [[0,64],[80,32],[112,32],[144,32],[176,32],[512,24],[288,20],[1120,64],[1120,96]];
const COMMAND = {ACQUIRE:1, HEARTBEAT:2, RELEASE:3, ESTOP:16, RECOVER:0x11, RPM:33};
function crc16(bytes) {
    let crc=65535;
    for (const value of bytes) {crc^=value;for(let bit=0;bit<8;bit++) crc=crc&1?(crc>>>1)^0xA001:crc>>>1;}
    return crc;
}
function frame(bytes) {const data=Buffer.from(bytes);const crc=crc16(data);return Buffer.concat([data,Buffer.from([crc&255,crc>>>8])]);}
function validFrame(data) {return data.length>=5 && crc16(data.subarray(0,-2))===data.readUInt16LE(data.length-2);}
function wordsToBytes(words) {const data=Buffer.alloc(words.length*2);words.forEach((value,index)=>data.writeUInt16BE(value&65535,index*2));return data;}
function bytesToWords(bytes) {if(bytes.length%2)throw Error('奇数载荷');return Array.from({length:bytes.length/2},(_,index)=>bytes.readUInt16BE(index*2));}
function put32(words,index,value,lowFirst=false) {const unsigned=value>>>0;words[index]=lowFirst?unsigned&65535:unsigned>>>16;words[index+1]=lowFirst?unsigned>>>16:unsigned&65535;}
function get32(words,index,lowFirst=false) {return lowFirst?((words[index+1]*65536+words[index])>>>0):((words[index]*65536+words[index+1])>>>0);}
function mailbox({boot,session=0,sequence,command,rpm=0,recoveryGeneration=0}) {
    const words=Array(24).fill(0);words[0]=0x484D;words[1]=1;put32(words,2,boot);put32(words,4,session);put32(words,6,sequence);words[8]=command;
    if(command===COMMAND.RPM){put32(words,10,rpm);words[17]=500;}
    if(command===COMMAND.RECOVER){put32(words,12,recoveryGeneration);}
    words[22]=2;words[23]=0xA55A;return words;
}
function writeFrame(words) {return frame(Buffer.concat([Buffer.from([1,16,1,0,0,24,48]),wordsToBytes(words)]));}
function readFrame(start,count) {return frame([1,3,start>>>8,start&255,count>>>8,count&255]);}
module.exports={REGIONS,COMMAND,crc16,frame,validFrame,wordsToBytes,bytesToWords,put32,get32,mailbox,writeFrame,readFrame};
