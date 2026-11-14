"""Generate bounded HMI33 scheduler/presentation from protected sources."""
from pathlib import Path
import json,re
R=Path(__file__).resolve().parent
E=R/'运行核验/33号实施_20261102';E.mkdir(exist_ok=True)
H='P8_ServoControl.h13.vals';S='P8_ServoControl.h13Status.vals';Q='P0_Boot.s33.vals'
def put(name,s): (R/name).write_text(s,encoding='utf-8-sig')
def sub(s): return s.replace('@H',H).replace('@S',S).replace('@Q',Q)

# s33: explicit schema, separate from legacy control indices.
schema={'estopGeneration':0,'estopSentGeneration':1,'cycle':2,'lastControlCycle':3,
'statusAdvanced':4,'lastMasterHi':5,'lastMasterLo':6,'masterSeen':7,'safeIdleRun':8,
'lastFullCycle':9,'fullCurrent':10,'lastReplySecond':11,'replySeen':12,'page':13,'fieldCursor':14,
'theme':15,'themeCursor':16,'themePending':17,'bootEpoch':18,'positionBaseline':19,
'baselineSeen':20,'lastSnapshotHi':21,'lastSnapshotLo':22,'snapshotSeen':23,'gapMs':24,
'stopAck':25,'acquireAck':26,'renderSkip':27,'auxCursor':28,'recoveryIntent':29,
'stopIntentGeneration':30,'stopConfirmedGeneration':31,'sourceEpoch':32,'metaCycle':33,
'latestStatusValid':34,'statusFailed':35,'lastFullMasterHi':36,'lastFullMasterLo':37,
'grantCycle':38,'transactionCount':39,'lastDisplayCount':40,'viewDirty':41,'localGeneration':42,
'estopInFlightGeneration':43,'protocolFault':44,'stopBaseHi':45,'stopBaseLo':46,
'stopBootHi':47,'stopBootLo':48,'stopWriteSeen':49,'fullSeen':50,'loopSecond':51,
'runsThisSecond':52,'lastRunsPerSecond':53,'fullSourceSequence':54,'lastRawSequence':55,'positionEpoch':56,'positionQualityCandidate':57,'positionQualityRun':58,'positionQuality':59}
(E/'33号状态索引.json').write_text(json.dumps(schema,indent=2),encoding='utf8')

def read(start,count,dest,offset=0,kind='aux'):
    cache=''
    if dest=='P0_Boot.rawRegs.vals':
        region={0:0,80:1,112:2,144:3,176:4}[start]
        # Atomic pairs are handled separately; never commit a partial invalid pair.
        paired={112:list(range(112,124))+list(range(130,132)),144:[148,149,150,151,152,153,158,159,164,165,166,167],176:[188,189,190,191]}.get(start,[])
        excluded=' && '.join(f'{offset}+i33!={j}' for j in paired) or '1==1'
        signed=[26,34,35,36,37,44,45,46,47,52,132,133,134,202]
        is_signed=' || '.join(f'{offset}+i33=={j}' for j in signed)
        cache=f'''for(i33=0;i33<{count};i33++){{v33={dest}[{offset}+i33];if(({excluded}) && ((v33!=65535 && v33!=32767) || (({is_signed}) && v33!=32767))){{if(P0_Boot.dRaw.vals[{offset}+i33]!=v33){{P0_Boot.dRev.vals[{region}]=P0_Boot.dRev.vals[{region}]+1;P0_Boot.dRaw.vals[{offset}+i33]=v33;}}}}}}
'''

        if start==112:
            cache=sub('''if(@Q[20]==1 && P0_Boot.rawRegs.vals[127]!=65535 && @Q[19]!=P0_Boot.rawRegs.vals[127]){
@Q[56]=@Q[56]+1;for(i33=116;i33<124;i33++){P0_Boot.dRaw.vals[i33]=65535;}P0_Boot.dRev.vals[2]=P0_Boot.dRev.vals[2]+1;
}
if(P0_Boot.rawRegs.vals[127]!=65535){@Q[20]=1;@Q[19]=P0_Boot.rawRegs.vals[127];}
v33=P0_Boot.rawRegs.vals[124];
if(v33==@Q[57]){@Q[58]=@Q[58]+1;}else{@Q[57]=v33;@Q[58]=1;}
if(@Q[58]>=2){@Q[59]=v33;}
''')+cache
        # Paired 32-bit quantities must be committed together, including valid
        # 0xFFFF low words and signed high words.
        pairs={112:[112,114,116,118,120,122,130],0:[],80:[],144:[148,150,152,158,164,166],176:[188,190]}
        for at in pairs.get(start,[]):
            gate='1==1'
            if at in [112,114]:gate=f'P0_Boot.rawRegs.vals[{140 if at==112 else 141}]==1'
            if at in [116,118,120,122]:gate='(P0_Boot.rawRegs.vals[124]&6)==2'
            if at==130:gate='P0_Boot.rawRegs.vals[142]==1'
            if at in [148,150,152]:gate='P0_Boot.rawRegs.vals[157]==1'
            if at in [164,166]:gate='P0_Boot.rawRegs.vals[172]==1'
            cache+=f'if({gate}){{if(P0_Boot.dRaw.vals[{at}]!={dest}[{at}] || P0_Boot.dRaw.vals[{at+1}]!={dest}[{at+1}]){{P0_Boot.dRev.vals[{region}]=P0_Boot.dRev.vals[{region}]+1;}}P0_Boot.dRaw.vals[{at}]={dest}[{at}];P0_Boot.dRaw.vals[{at+1}]={dest}[{at+1}];}}\n'
    if kind=='status':
        cache=f'''for(i33=0;i33<{count};i33++){{v33={S}[i33];if(i33<18 || i33>24 || v33!=32767){{if(P0_Boot.dStatus.vals[i33]!=v33 && (i33<4 || i33>7) && i33!=14 && i33!=15 && i33!=16 && i33!=17){{P0_Boot.dRev.vals[{5 if count==64 else 6}]=P0_Boot.dRev.vals[{5 if count==64 else 6}]+1;}}P0_Boot.dStatus.vals[i33]=v33;}}}}
'''
    return sub(f'''@Q[39]=@Q[39]+1;
r33=modbusRead(1,3,{start},{count},50);ok33=0;
if(r33==0 && P0_Boot.mod.addr==1 && P0_Boot.mod.fcode==3 && P0_Boot.mod.rxLen=={count*2+1} && P0_Boot.mod.rxBuf[0]=={count*2}){{
ok33=1;
'''+ ('if(@H[74]!=1 || bytesToShort(P0_Boot.mod.rxBuf,5,1)!=@H[4] || bytesToShort(P0_Boot.mod.rxBuf,7,1)!=@H[5]){ok33=0;}' if kind=='status' else '')+ ('if(bytesToShort(P0_Boot.mod.rxBuf,1,1)!=21325 || bytesToShort(P0_Boot.mod.rxBuf,3,1)!=2){ok33=0;}' if kind=='status' else 'if(bytesToShort(P0_Boot.mod.rxBuf,1,1)!=21065 || bytesToShort(P0_Boot.mod.rxBuf,3,1)!=1){ok33=0;}' if kind=='meta' else '')+f'''
if(ok33==1){{
for(i33=0;i33<{count};i33++){{{dest}[{offset}+i33]=bytesToShort(P0_Boot.mod.rxBuf,1+i33*2,1)&65535;}}
{cache}
@Q[11]=sys_run_time;@Q[12]=1;@H[104]=@H[104]+1;@H[107]=@H[1];@H[45]=0;
}}
}}
if(ok33==0){{@H[77]=@H[77]+1;@H[45]=2;@Q[27]=1;{sub('@H[2]=6;@H[10]=0;@H[3]=0;') if kind=='status' else ''}}}
''')

process=sub('''
@Q[4]=0;@Q[34]=0;
if(ok33==1 && @H[74]==1 && @S[2]==@H[4] && @S[3]==@H[5]){
@Q[34]=1;@H[86]=1;@H[20]=1;@H[21]=@H[1];
// Compare uint32 sequence halves explicitly; do not depend on signed overflow.
hi33=(@S[4]-@Q[21]+65536)%65536;seqDelta33=0;
if((hi33==0 && @S[5]>@Q[22]) || (hi33>0 && hi33<32768) || (hi33==32768 && @S[5]<@Q[22])){seqDelta33=1;}
if(@Q[23]==0 || seqDelta33>0){
 @Q[4]=1;@Q[21]=@S[4];@Q[22]=@S[5];@Q[23]=1;@H[73]=@H[1];
 gap33=0;if(@Q[7]==1){hi33=(@S[6]-@Q[5]+65536)%65536;gap33=300;if(hi33<=1){gap33=hi33*65536+@S[7]-@Q[6];}}
 @Q[24]=gap33;@Q[5]=@S[6];@Q[6]=@S[7];@Q[7]=1;
 if(@H[8]<@S[16] || (@H[8]==@S[16] && @H[9]<@S[17])){@H[8]=@S[16];@H[9]=@S[17];}
}
if(@Q[4]==1){
 safe33=0;
 if(@S[8]==0 && @S[12]==0 && @S[13]==0 && (@S[11]&102)==0 && @S[37]==0 && @S[9]>0 && @S[9]<=3 && (@S[25]&@S[9])==@S[9] && (@S[26]&@S[9])==@S[9] && (@S[27]&@S[9])==@S[9] && ((@S[9]&1)==0 || (@S[23]==0 && @S[46]<500)) && ((@S[9]&2)==0 || (@S[24]==0 && @S[47]<500))){safe33=1;}
 if(safe33==1){if(@Q[8]<2){@Q[8]=@Q[8]+1;}}else{@Q[8]=0;}
 if(@H[2]==3){
  if(gap33<0 || gap33>=300 || @S[15]>=300 || @S[14]<=200 || @S[8]!=1 || (@S[11]&102)!=2 || @S[12]!=@H[6] || @S[13]!=@H[7]){
   @H[2]=6;@H[3]=0;@H[10]=0;@H[31]=1;@H[83]=1;@Q[27]=1;
   if(@S[8]==1 && (@S[11]&4)==0){@H[39]=16;}
  }
 }
 if(@H[2]==2 && @Q[26]==1 && @S[8]==1 && (@S[11]&102)==2 && @S[12]==P8_ServoControl.h13Result.vals[3] && @S[13]==P8_ServoControl.h13Result.vals[2]){
  @H[6]=@S[12];@H[7]=@S[13];@H[2]=3;@H[10]=0;@H[26]=0;@H[30]=0;@H[31]=0;@H[83]=0;@H[138]=1;
  @H[59]=@S[48];@H[60]=@S[49];@Q[26]=0;@Q[25]=0;@Q[49]=0;@Q[38]=@Q[2];
  @H[32]=1;P8_ServoControl.rpmSlider.val=1600;@H[32]=0;
 }
 if(@Q[8]>=2 && @Q[0]==@Q[1] && @H[116]==0 && @H[2]!=2 && @H[2]!=3){
  if(@H[2]!=4 || (@Q[25]==1 && @Q[49]==1 && @Q[47]==@H[4] && @Q[48]==@H[5] && ((@S[50]==@H[27] && @S[51]==@H[28] && @S[52]==@H[26] && @S[53]==3 && (@S[48]!=@Q[45] || @S[49]!=@Q[46])) || (@S[48]==@Q[45] && @S[49]==@Q[46] && P8_ServoControl.h13Result.vals[7]==3 && @S[10]==0)))){
   @H[2]=1;if(@S[53]==3){@H[2]=5;}@H[6]=0;@H[7]=0;@H[10]=0;@H[26]=0;@H[39]=0;@H[31]=0;@H[83]=0;@H[30]=0;@Q[49]=0;
   @H[32]=1;P8_ServoControl.rpmSlider.val=1600;@H[32]=0;
  }
 }
 if(@H[2]==0){@H[2]=6;}
 if(@H[2]==6 && @S[8]==1 && (@S[11]&4)==0){@H[39]=16;}
 if(@Q[10]==1){
  @Q[9]=@Q[2];@Q[50]=1;@Q[36]=@S[6];@Q[37]=@S[7];@Q[54]=@Q[54]+1;
  if(@S[89]>=1 && @S[89]<=5){@H[10]=0;if(@H[2]==3){@H[2]=6;}}
  if(@S[89]==6 && @H[133]==3){@H[133]=6;@H[26]=0;@H[2]=6;}
  if(@S[89]==7){@H[133]=7;@H[136]=100+@S[93];}
 }
}
if(@Q[4]==1){@Q[35]=0;}else if(@H[2]==3){@Q[35]=@Q[35]+1;if(@Q[35]>=6){@H[2]=6;@H[3]=0;@H[10]=0;@H[31]=1;@Q[27]=1;}}
}else{@Q[34]=0;@H[86]=0;}
''')

def status(full):
    n=96 if full else 64
    return sub(f'@Q[10]={1 if full else 0};\n')+read(1120,n,S,kind='status')+process

write=sub('''
if(cmd33!=0 && @H[74]==1 && @Q[44]==0){
 if(@H[8]==65535 && @H[9]>=65534 && cmd33!=16){cmd33=16;@H[2]=6;@H[10]=0;}
 if(@H[8]==65535 && @H[9]==65535){@H[75]=1;@H[2]=6;cmd33=0;}
 if(cmd33!=0){
 @H[9]=@H[9]+1;if(@H[9]>65535){@H[9]=0;@H[8]=@H[8]+1;}
 for(i33=0;i33<48;i33++){P8_ServoControl.h13Tx.valbs[i33]=(byte)(0);}
 P8_ServoControl.h13Tx.valbs[0]=(byte)(72);P8_ServoControl.h13Tx.valbs[1]=(byte)(77);P8_ServoControl.h13Tx.valbs[3]=(byte)(1);
 P8_ServoControl.h13Tx.valbs[4]=(byte)(@H[4]/256);P8_ServoControl.h13Tx.valbs[5]=(byte)(@H[4]&255);P8_ServoControl.h13Tx.valbs[6]=(byte)(@H[5]/256);P8_ServoControl.h13Tx.valbs[7]=(byte)(@H[5]&255);
 if(cmd33==33 || cmd33==3){P8_ServoControl.h13Tx.valbs[8]=(byte)(@H[6]/256);P8_ServoControl.h13Tx.valbs[9]=(byte)(@H[6]&255);P8_ServoControl.h13Tx.valbs[10]=(byte)(@H[7]/256);P8_ServoControl.h13Tx.valbs[11]=(byte)(@H[7]&255);}
 P8_ServoControl.h13Tx.valbs[12]=(byte)(@H[8]/256);P8_ServoControl.h13Tx.valbs[13]=(byte)(@H[8]&255);P8_ServoControl.h13Tx.valbs[14]=(byte)(@H[9]/256);P8_ServoControl.h13Tx.valbs[15]=(byte)(@H[9]&255);
 P8_ServoControl.h13Tx.valbs[17]=(byte)(cmd33);P8_ServoControl.h13Tx.valbs[45]=(byte)(2);P8_ServoControl.h13Tx.valbs[46]=(byte)(165);P8_ServoControl.h13Tx.valbs[47]=(byte)(90);
 if(cmd33==33){v33=@H[10];hi33=0;if(v33<0){hi33=65535;v33=v33+65536;}P8_ServoControl.h13Tx.valbs[20]=(byte)(hi33/256);P8_ServoControl.h13Tx.valbs[21]=(byte)(hi33&255);P8_ServoControl.h13Tx.valbs[22]=(byte)(v33/256);P8_ServoControl.h13Tx.valbs[23]=(byte)(v33&255);P8_ServoControl.h13Tx.valbs[34]=(byte)(1);P8_ServoControl.h13Tx.valbs[35]=(byte)(244);}
 if(cmd33==17){P8_ServoControl.h13Tx.valbs[24]=(byte)(@H[125]/256);P8_ServoControl.h13Tx.valbs[25]=(byte)(@H[125]&255);P8_ServoControl.h13Tx.valbs[26]=(byte)(@H[126]/256);P8_ServoControl.h13Tx.valbs[27]=(byte)(@H[126]&255);}
 if(cmd33!=33){@H[26]=cmd33;@H[27]=@H[8];@H[28]=@H[9];@H[29]=@Q[2];}
 if(cmd33==16 || cmd33==3){@Q[45]=@S[48];@Q[46]=@S[49];@Q[47]=@H[4];@Q[48]=@H[5];@Q[25]=0;@Q[49]=1;@Q[8]=0;@H[2]=4;@H[10]=0;@Q[43]=@Q[0];}
 @Q[39]=@Q[39]+1;@H[78]=@H[78]+1;
 r33=modbusWrites(1,16,256,24,P8_ServoControl.h13Tx.valbs,48,50);
 ok33=0;if(r33==0 && P0_Boot.mod.addr==1 && P0_Boot.mod.fcode==16 && P0_Boot.mod.rxLen==4 && bytesToShort(P0_Boot.mod.rxBuf,0,1)==256 && bytesToShort(P0_Boot.mod.rxBuf,2,1)==24){ok33=1;@Q[11]=sys_run_time;@Q[12]=1;}
 if(ok33==1){
  if(cmd33==16 && @Q[0]==@Q[43]){@Q[1]=@Q[43];@H[116]=0;}
  if(cmd33==33){@H[138]=0;}
 }else{@Q[27]=1;@H[2]=6;@H[3]=0;@H[10]=0;@H[31]=1;@H[83]=1;@H[77]=@H[77]+1;}
 @H[39]=0;
 }
}
''')

meta=read(512,24,'P0_Boot.metaRegs.vals',kind='meta')+sub('''
if(ok33==1){
 @Q[44]=0;if(P0_Boot.metaRegs.vals[3]!=2 || (P0_Boot.metaRegs.vals[2]&240)!=240){@Q[44]=1;}
 if(@H[74]==0 || @H[4]!=P0_Boot.metaRegs.vals[4] || @H[5]!=P0_Boot.metaRegs.vals[5]){
  @H[4]=P0_Boot.metaRegs.vals[4];@H[5]=P0_Boot.metaRegs.vals[5];@H[74]=1;@H[8]=0;@H[9]=0;@H[6]=0;@H[7]=0;@H[10]=0;@H[2]=6;@H[26]=0;@H[39]=0;@H[133]=0;@Q[26]=0;@Q[25]=0;@Q[8]=0;@Q[7]=0;@Q[23]=0;@Q[18]=@Q[18]+1;@Q[50]=0;@Q[20]=0;@Q[56]=@Q[56]+1;
  for(i33=0;i33<208;i33++){P0_Boot.dRaw.vals[i33]=65535;}for(i33=0;i33<96;i33++){P0_Boot.dStatus.vals[i33]=65535;if(i33>=18 && i33<=24){P0_Boot.dStatus.vals[i33]=32767;}}
  for(i33=0;i33<5;i33++){P0_Boot.regionSeen.vals[i33]=0;}
  @H[32]=1;P8_ServoControl.rpmSlider.val=1600;@H[32]=0;
 }
 @H[47]=1;@H[22]=@H[1];P0_Boot.metaSeen.vals[0]=1;P0_Boot.metaValid.vals[0]=1;@Q[33]=@Q[2];
}
''')

result=read(288,20,'P8_ServoControl.h13Result.vals')+sub('''
if(ok33==1 && P8_ServoControl.h13Result.vals[0]==@H[5] && P8_ServoControl.h13Result.vals[1]==@H[4] && P8_ServoControl.h13Result.vals[4]==@H[28] && P8_ServoControl.h13Result.vals[5]==@H[27] && P8_ServoControl.h13Result.vals[6]==@H[26]){
 if(P8_ServoControl.h13Result.vals[7]==3){
  if(@H[26]==1){@Q[26]=1;}
  if(@H[26]==16 || @H[26]==3){@Q[25]=1;}
 }
 if(@H[26]==17 && (P8_ServoControl.h13Result.vals[7]==2 || P8_ServoControl.h13Result.vals[7]==3)){@H[133]=3;}
 if(P8_ServoControl.h13Result.vals[7]>=4){@H[30]=P8_ServoControl.h13Result.vals[8];@H[2]=6;@H[10]=0;@H[31]=1;if(@H[26]==17){@H[133]=7;@H[136]=200+@H[30];}@H[26]=0;}
}
''')

service=sub('''int i33;
int cmd33;
int r33;
int ok33;
int v33;
int hi33;
int gap33;
int safe33;
int did33;
int seqDelta33;
// One global serial scheduler. Cycles schedule work; never represent elapsed ms.
if(@H[80]==0){
@H[80]=1;@Q[27]=0;did33=0;cmd33=0;
@Q[2]=@Q[2]+1;@H[1]=@Q[2]*50;@H[96]=@H[96]+1;
if(@Q[51]!=sys_run_time){@Q[53]=@Q[52];@Q[52]=0;@Q[51]=sys_run_time;}@Q[52]=@Q[52]+1;
if(@H[116]==1 && @Q[0]==@Q[1]){@Q[0]=@Q[0]+1;}
if(@Q[0]!=@Q[1] || @H[116]==1){cmd33=16;@H[10]=0;}
else if(@H[11]==3){cmd33=3;if(@H[6]==0 && @H[7]==0){cmd33=16;}@H[10]=0;}
if(@H[11]==16){cmd33=16;@H[10]=0;}
if(cmd33!=0 && @H[74]==1){
''')+write+sub('''did33=1;@H[11]=0;
}
if(did33==0 && (@H[74]==0 || @Q[44]==1 || (@H[2]!=3 && @Q[2]-@Q[33]>=40))){
''')+meta+sub('''did33=1;}
if(did33==0 && @H[2]==3 && @Q[2]-@Q[3]>=3){
''')+'''if('''+Q+'''[2]-'''+Q+'''[9]>=20){
'''+status(True)+'''}else{
'''+status(False)+sub('''}
@Q[3]=@Q[2];did33=1;
if(@Q[4]==1 && @Q[34]==1 && @H[2]==3 && @Q[0]==@Q[1] && @H[116]==0){cmd33=33;
''')+write+sub('''}
else{if(@H[2]==3){@H[2]=6;@H[10]=0;@H[3]=0;}@Q[27]=1;}
}
if(did33==0 && @H[11]==1){
''')+status(True)+sub('''
if(@Q[4]==1 && @Q[8]>=2 && @Q[44]==0 && (@H[2]==1 || @H[2]==5) && @Q[0]==@Q[1]){cmd33=1;@H[2]=2;@H[10]=0;@Q[26]=0;@Q[8]=0;@Q[25]=0;@Q[49]=0;
''')+write+sub('''}
if(cmd33==1 || @Q[34]==0 || @S[8]!=0 || @Q[44]==1){@H[11]=0;}did33=1;}
if(did33==0 && @Q[29]==1){
''')+status(True)+sub('''
if(@Q[4]==1 && @S[88]==1 && @S[89]==2 && @S[90]==1 && (@H[125]!=@S[91] || @H[126]!=@S[92])){
 @H[125]=@S[91];@H[126]=@S[92];@H[133]=2;@H[10]=0;cmd33=17;
''')+write+sub('''}else{@H[136]=3;}@Q[29]=0;did33=1;}
if(did33==0 && @H[26]!=0){
''')+result+status(True)+sub('''did33=1;
if(@H[2]==3 && @H[138]==1 && @Q[4]==1 && @Q[0]==@Q[1]){cmd33=33;@Q[3]=@Q[2];
''')+write+sub('''}
if(@H[26]==1 && @Q[2]-@H[29]>46){@H[26]=0;@H[2]=6;@H[39]=16;}
}
if(did33==0 && (@H[2]!=3 || @Q[2]-@Q[33]>=40)){
if(@Q[2]-@Q[3]>=3 || @H[39]==16){
''')+status(True)+sub('''@Q[3]=@Q[2];did33=1;
if(@H[39]==16 && @Q[34]==1 && @S[8]==1 && (@S[11]&4)==0){cmd33=16;
''')+write+sub('''}
}}
// Auxiliary work consumes idle slots only. No catch-up queue.
if(did33==0){
if(@Q[2]-@Q[33]>=40){
''')+meta+sub('''}else{
@Q[28]=(@Q[28]+1)%5;
''')
for region,(start,n) in enumerate([(0,64),(80,32),(112,32),(144,32),(176,32)]):
    service+=sub(f'if(@Q[28]=={region}){{\n')+read(start,n,'P0_Boot.rawRegs.vals',start)+sub(f'''if(ok33==1){{P0_Boot.regionSeen.vals[{region}]=1;P0_Boot.regionValid.vals[{region}]=1;P0_Boot.regionAge.vals[{region}]=0;P0_Boot.regionBadRun.vals[{region}]=0;@H[{109+region}]=@H[{109+region}]+1;P0_Boot.regionGoodRun.vals[{region}]=2;}}}}
''')
service+=sub('''}}
@H[3]=0;if(@Q[34]==1 && @Q[44]==0 && @Q[0]==@Q[1] && (@H[2]==1 || @H[2]==3 || @H[2]==5)){@H[3]=1;}
if(@H[2]!=3){@H[10]=0;}
@H[80]=0;
}
''')
put('HMI33事务服务.txt',service)
put('事件/授权_按下.txt',sub('if(@H[2]==3){@H[11]=3;}else if(@H[2]==2){@Q[0]=@Q[0]+1;@H[116]=1;@H[11]=16;}else if(@H[2]==1 || @H[2]==5){@H[11]=1;}@Q[42]=@Q[42]+1;\n'))
put('事件/授权_弹起.txt','// HMI33 ordinary button: release has no control action.\n')
put('事件/急停_按下.txt',sub('@Q[0]=@Q[0]+1;@H[116]=1;@H[11]=16;@H[10]=0;@Q[42]=@Q[42]+1;\n'))
put('事件/故障恢复按钮_按下.txt',sub('@Q[29]=1;@Q[42]=@Q[42]+1;\n'))
put('事件/滑块_滑动.txt',sub('if(@H[2]==3 && @H[3]==1 && @H[32]==0 && @H[138]==0 && @Q[0]==@Q[1] && @H[116]==0){@H[10]=P8_ServoControl.rpmSlider.val-1600;}@Q[42]=@Q[42]+1;\n'))
print('Generated service',len(service))

# A small C-like parser lets each field retain only its own control flow and
# numeric calculations. Hidden pages never execute a formatter.
original=(R.parent/'31_轨道巡检车日夜双主题切换版/全局计龄刷新.txt').read_text(encoding='utf-8-sig')
original=re.sub(r'//[^\n]*','',original)
bootmodel=json.loads((R/'运行核验/32号实施_20261102/32号实际工程读回.json').read_text(encoding='utf-8-sig'))
for ctl in bootmodel['Pages'][0]['Controls']:
 original=re.sub(r'(?<![.\w])'+re.escape(ctl['Name'])+r'\.', 'P0_Boot.'+ctl['Name']+'.', original)

decls=re.findall(r'^int \w+;',original,re.M)
original=re.sub(r'^int \w+;','',original,flags=re.M)
class Parser:
 def __init__(self,s):self.s=s;self.p=0
 def ws(self):
  while self.p<len(self.s) and self.s[self.p].isspace():self.p+=1
 def balanced(self):
  self.ws();start=self.p;depth=0;quoted=False
  while self.p<len(self.s):
   c=self.s[self.p];self.p+=1
   if c=='"' and (self.p<2 or self.s[self.p-2]!='\\'):quoted=not quoted
   if not quoted:
    if c=='(':depth+=1
    if c==')':
     depth-=1
     if depth==0:return self.s[start:self.p]
  raise ValueError('unbalanced')
 def statement(self):
  self.ws()
  if self.p>=len(self.s):return None
  if self.s[self.p]=='{':
   self.p+=1;nodes=[];self.ws()
   while self.p<len(self.s) and self.s[self.p]!='}':nodes.append(self.statement());self.ws()
   self.p+=1;return ['block',nodes]
  for kw in ['if','for']:
   if re.match(kw+r'\s*\(',self.s[self.p:]):
    self.p+=len(kw);cond=self.balanced();body=self.statement();other=None;self.ws()
    if kw=='if' and self.s[self.p:self.p+4]=='else':self.p+=4;other=self.statement()
    return [kw,cond,body,other]
  start=self.p;quoted=False
  while self.p<len(self.s):
   c=self.s[self.p];self.p+=1
   if c=='"' and (self.p<2 or self.s[self.p-2]!='\\'):quoted=not quoted
   if c==';' and not quoted:return ['stmt',self.s[start:self.p]]
  return ['stmt',self.s[start:self.p]]
 def all(self):
  nodes=[]
  while True:
   n=self.statement()
   if n is None:break
   nodes.append(n)
  return ['block',nodes]
tree=Parser(original).all()
locals_set={re.search(r'int (\w+)',d)[1] for d in decls}
def has_target(n,key):
 if n is None:return False
 if n[0]=='stmt':return re.search(re.escape(key)+r'\.(txt|fontColor|font)=',n[1]) is not None
 if n[0]=='block':return any(has_target(x,key) for x in n[1])
 return has_target(n[2],key) or has_target(n[3],key)
def valid_text(expr,key):
 literals=''.join(re.findall(r'"([^"\n]*)"',expr))
 bad=any(x in literals for x in ['未知','--','已过期','无效/过期','越界','UNKNOWN','未建立','OFFLINE','无效','未知阶段'])
 if key.startswith('P3_Position.') and any(x in key for x in ['startup','segmentTravel','axisPositionDiff']):bad|='不完整' in literals
 return not bad
def sliced(n,key,needed,page):
 if n is None:return ''
 if n[0]=='stmt':
  s=n[1];m=re.fullmatch(re.escape(key)+r'\.(txt|fontColor|font)=(.*);',s,re.S)
  if m:
   prop,expr=m.groups()
   if prop=='txt':
    if not valid_text(expr,key):return ''
    needed.update(set(re.findall(r'\b\w+\b',expr))&locals_set)
    return 'u33text='+expr.replace(key+'.txt','u33text')+';u33valid=1;'
   needed.update(set(re.findall(r'\b\w+\b',expr))&locals_set)
   return ('u33color=' if prop=='fontColor' else 'u33font=')+expr+';'
  m=re.match(r'(\w+)\s*=(.*);',s,re.S)
  if m and m[1] in needed:
   needed.discard(m[1]);needed.update(set(re.findall(r'\b\w+\b',m[2]))&locals_set);return s
  return ''
 if n[0]=='block':
  out=[]
  for x in reversed(n[1]):out.append(sliced(x,key,needed,page))
  return ''.join(reversed(out))
 # All display fragments are generated for exactly one page.
 pm=re.fullmatch(r'\(sys_pid==([A-Za-z0-9_]+)\.id\)',n[1])
 if pm:return sliced(n[2] if pm[1]==page else n[3],key,needed,page)
 na=set(needed);nb=set(needed)
 a=sliced(n[2],key,na,page);b=sliced(n[3],key,nb,page)
 if not a and not b:return ''
 needed.clear();needed.update(na|nb);needed.update(set(re.findall(r'\b\w+\b',n[1]))&locals_set)
 if n[0]=='for':return 'for'+n[1]+'{'+a+'}'
 return 'if'+n[1]+'{'+a+'}'+('else{'+b+'}' if b else '')

model=json.loads((R/'运行核验/32号实施_20261102/32号实际工程读回.json').read_text(encoding='utf-8-sig'))
resources=json.loads((R/'运行核验/31号实施_20261101/资源索引绑定.json').read_text(encoding='utf-8-sig'))
fields=[];page_fields={};code=[]
for p in model['Pages']:
 page_fields[p['Id']]=[]
 for c in p['Controls']:
  if not c['Type'].endswith(('TextCtrl','ButtonCtrl')):continue
  key=p['Name']+'.'+c['Name']
  if not has_target(tree,key):continue
  needed=set();fragment=sliced(tree,key,needed,p['Name'])
  # Display reads only numeric history. No cached data enters control.
  fragment=fragment.replace('P0_Boot.rawRegs.vals','P0_Boot.dRaw.vals').replace('P0_Boot.h13Status.vals','P0_Boot.dStatus.vals').replace('P8_ServoControl.h13Status.vals','P0_Boot.dStatus.vals')
  fragment=re.sub(r'(?<!\.)\brawRegs.vals','P0_Boot.dRaw.vals',fragment)
  fragment=re.sub(r'(?<!\.)\b(regionValid|regionSeen).vals\[(\d+)\]',r'P0_Boot.regionSeen.vals[\2]',fragment)
  fragment=re.sub(r'(?:P0_Boot\.)?regionBadRun.vals\[\d+\]<10','1==1',fragment)
  fragment=re.sub(re.escape(H)+r'\[1\]-'+re.escape(H)+r'\[(?:21|73)\]<300','1==1',fragment)
  fragment=fragment.replace(H+'[86]==1',Q+'[23]==1')
  if p['Id']==8 and c['Name'] in ['authorityStatus','errorStatus','linkStatus','commonTarget','sliderSpeedHint','recoveryStatus','recoveryDetail','recoveryButton','stopStatus']:
   continue
  ix=len(fields);fields.append({'index':ix,'key':key,'page':p['Id'],'default':c['Properties'].get('Text',''),'font':next((i+1 for i,f in enumerate(model['Fonts']) if f['Id']==c['Properties'].get('FontId')),1)})
  page_fields[p['Id']].append(ix)
  code.append((ix,fragment))

def add(name,fragment,page=8):
 p=model['Pages'][page];c=next(x for x in p['Controls'] if x['Name']==name);ix=len(fields)
 fields.append({'index':ix,'key':p['Name']+'.'+name,'page':page,'default':c['Properties'].get('Text',''),'font':next((i+1 for i,f in enumerate(model['Fonts']) if f['Id']==c['Properties'].get('FontId')),1)})
 page_fields[page].append(ix);code.append((ix,sub(fragment)))
add('authorityStatus','''u33valid=1;u33text="未授权";if(@H[2]==2){u33text="申请中，等待主控确认";}if(@H[2]==3){u33text="已授权：新滑动调整共同目标";u33color=P0_Boot.themeGood.val;}if(@H[2]==4){u33text="正在停车 / 等待主控确认";}if(@H[2]==5){u33text="停车完成，可重新授权";}if(@H[2]==6){u33text="通信待确认 / 禁止恢复旧目标";}if(@H[2]==7){u33text="故障锁定 / 检查主控和驱动";}''')
add('errorStatus','''u33valid=1;u33text="状态 S"+intToString(@H[2])+" 错误 "+intToString(@H[30])+" 停车原因 "+intToString(@S[29]);''')
add('linkStatus','''u33valid=1;u33text="通信：身份待确认";if(@Q[12]==1 && @Q[23]==1){u33text="通信：正常 / R2接口匹配";u33color=P0_Boot.themeGood.val;}if(@Q[12]==1 && sys_run_time-@Q[11]>=3){u33text="通信：超时";u33color=P0_Boot.themeFault.val;}if(@Q[44]==1){u33text="通信：协议不匹配";}''')
add('commonTarget','''u33valid=1;u33text="设定："+intToString(@H[10])+"rpm|"+floatToString(@H[10]*0.00363246650571,3)+"m/s|"+floatToString(@H[10]*0.01307687942057,3)+"km/h";''')
add('sliderSpeedHint','''u33valid=1;u33text="预览："+floatToString((P8_ServoControl.rpmSlider.val-1600)*0.01307687942057,3)+"km/h";if(@H[2]==3){u33text="设定："+floatToString(@H[10]*0.01307687942057,3)+"km/h";}''')
add('stopStatus','''if(@Q[23]==1){u33valid=1;u33text="停车阶段：无";if(@S[30]>=1 && @S[30]<=3){u33text="停车：按要求减速";}if(@S[30]==4 && @S[8]==0){u33text="已停车，未授权";}if(@S[30]==5){u33text="停车失败 / 检查故障";}}''')
add('recoveryStatus','''if(@Q[50]==1){u33valid=1;u33text="无需恢复";if(@S[89]==1){u33text="等待健康";}if(@S[89]==2){u33text="可恢复";}if(@S[89]>=3 && @S[89]<=5){u33text="恢复中";}if(@S[89]==6){u33text="恢复完成，请重新授权";}if(@S[89]==7){u33text="恢复失败";}}''')
add('recoveryDetail','''if(@Q[50]==1){u33valid=1;u33text="原因 "+intToString(@S[93])+" 首错轴/项 "+intToString(@S[94]&255)+"/"+intToString(@S[94]/256)+" 健康 "+intToString(@S[95])+"ms";}''')
add('recoveryButton','u33valid=1;u33text="故障恢复";')
for page in range(1,9):
 if not any(c['Name']=='dataQuality' for c in model['Pages'][page]['Controls']):continue
 add('dataQuality',sub('''u33valid=1;u33text="数据：等待首帧";if(@Q[12]==1){u33text="数据：保留最近有效值";if(sys_run_time-@Q[11]>=3){u33text="通信超时，保留最近值 "+intToString(sys_run_time-@Q[11])+"s";u33color=P0_Boot.themeWarn.val;}}'''),page)
for ix,f in enumerate(fields):
 if f['key']=='P3_Position.positionIncomplete':
  code=[(j,sub('''if(@Q[20]==1 && @Q[58]>=2){u33valid=1;u33text="里程质量：完整";if((@Q[59]&4)!=0){u33text="里程质量：不完整，保留最近值";u33color=P0_Boot.themeWarn.val;}if((@Q[59]&2)==0){u33text="里程质量：待建立，保留最近值";}}''') if j==ix else t) for j,t in code]
# Current permission is never a retained business value.
for ix,f in enumerate(fields):
 if f['key']=='P1_Main.controlAuthority':code=[(j,sub('u33valid=1;u33text="屏幕控制：未授权";if(@H[2]==3){u33text="屏幕控制：已授权";}if(@H[2]==6){u33text="屏幕控制：待同步";}') if j==ix else t) for j,t in code]
 if f['key']=='P1_Main.ctrlComm':code=[(j,sub('u33valid=1;u33text="控制器通信：等待主控";if(@Q[12]==1){u33text="控制器通信：正常";if(sys_run_time-@Q[11]>=3){u33text="控制器通信：超时";}}') if j==ix else t) for j,t in code]
 if f['key']=='P4_Info.screenVersion':code=[(j,'u33valid=1;u33text="屏幕工程：33";' if j==ix else t) for j,t in code]

formatter='\n'.join(decls)+sub('''
int u33index;
int u33n;
int u33valid;
int u33color;
int u33font;
int u33start;
int u33end;
int u33theme;
int u33j;
formalProtocol=0;if(@H[74]==1 && @Q[44]==0){formalProtocol=1;}
baseDisplayFresh=P0_Boot.regionSeen.vals[0];
if(@Q[13]!=sys_pid || @Q[15]!=P0_Boot.themeMode.val){P0_Boot.dRev.vals[8]=P0_Boot.dRev.vals[8]+1;}
if(P0_Boot.dLocal.vals[0]!=@H[2] || P0_Boot.dLocal.vals[1]!=@H[10] || P0_Boot.dLocal.vals[2]!=@H[30] || P0_Boot.dLocal.vals[3]!=P8_ServoControl.rpmSlider.val || P0_Boot.dLocal.vals[4]!=sys_run_time || P0_Boot.dLocal.vals[5]!=@Q[59]){
P0_Boot.dLocal.vals[0]=@H[2];P0_Boot.dLocal.vals[1]=@H[10];P0_Boot.dLocal.vals[2]=@H[30];P0_Boot.dLocal.vals[3]=P8_ServoControl.rpmSlider.val;P0_Boot.dLocal.vals[4]=sys_run_time;P0_Boot.dLocal.vals[5]=@Q[59];P0_Boot.dRev.vals[7]=P0_Boot.dRev.vals[7]+1;}

@Q[40]=0;
if(@Q[13]!=sys_pid || @Q[15]!=P0_Boot.themeMode.val){@Q[13]=sys_pid;@Q[15]=P0_Boot.themeMode.val;@Q[14]=0;@Q[16]=0;@Q[17]=1;}
u33start=0;u33end=0;
''')
def choose(items,var):
 if not items:return ''
 if len(items)==1:return items[0][1]
 mid=len(items)//2
 return f'if({var}<{items[mid][0]}){{'+choose(items[:mid],var)+'}else{'+choose(items[mid:],var)+'}'
order=[]
for page in range(0,9):
 ids=page_fields[page];start=len(order);order+=ids
 formatter+=f'if(sys_pid=={page}){{u33start={start};u33end={len(order)};}}\n'
# Two status slots precede the rotating business slots and survive a failed read.
urgent={1:['P1_Main.ctrlComm','P1_Main.controlAuthority'],8:['P8_ServoControl.authorityStatus','P8_ServoControl.linkStatus']}
for page,keys in urgent.items():
 for key in keys:
  ix=next(f['index'] for f in fields if f['key']==key)
  formatter+=f'if(sys_pid=={page}){{HMI33Field{ix}();{Q}[40]={Q}[40]+1;}}'
formatter+=sub('''if(@Q[14]<u33start || @Q[14]>=u33end){@Q[14]=u33start;}
if(@Q[27]==0 && u33end>u33start){
for(u33n=0;u33n<4;u33n++){
''')
leaves=[]
for j,ix in enumerate(order):
 f=fields[ix];k=f['key'];frag=dict(code)[ix]
 epoch=Q+'[18]+1'
 if k in ['P3_Position.startupDisplacement','P3_Position.startupTravel','P3_Position.segmentTravel','P3_Position.axisPositionDiff']:epoch=Q+'[18]*100000+'+Q+'[56]+1'
 deps=set()
 for v in re.findall(r'P0_Boot.dRaw.vals\[(\d+)\]',frag):
  n=int(v);deps.add(0 if n<64 else 1 if n<112 else 2 if n<144 else 3 if n<176 else 4)
 if 'dStatus.vals' in frag or S in frag:deps.update([5,6])
 if H in frag or Q in frag or 'rpmSlider.val' in frag:deps.add(7)
 deps.add(8)
 checks=[f'P0_Boot.dStamp.vals[{ix*9+d}]!=P0_Boot.dRev.vals[{d}]' for d in sorted(deps)]
 leaf='if('+ ' || '.join(checks)+f' || P0_Boot.dField.vals[{ix}]!={epoch})'+'{'
 for d in sorted(deps):leaf+=f'P0_Boot.dStamp.vals[{ix*9+d}]=P0_Boot.dRev.vals[{d}];'
 leaf+='u33valid=0;u33text="";u33color=P0_Boot.themeInk.val;u33font=0;'+frag
 leaf+=f'''if(u33valid==1){{if({k}.txt!=u33text){{{k}.txt=u33text;}}if({k}.fontColor!=u33color){{{k}.fontColor=u33color;}}if(u33font>0 && {k}.font!=u33font){{{k}.font=u33font;}}P0_Boot.dField.vals[{ix}]={epoch};}}
else{{if(P0_Boot.dField.vals[{ix}]!={epoch}){{{k}.txt={json.dumps(f['default'],ensure_ascii=False)};P0_Boot.dField.vals[{ix}]={epoch};}}}}
'''
 leaf+='}'
 leaves.append((j,leaf))
methods=[]
for j,leaf in leaves:
 ix=order[j]
 body='\n'.join(decls)+'''int u33valid;int u33color;int u33font;'''+sub('''formalProtocol=0;if(@H[74]==1 && @Q[44]==0){formalProtocol=1;}baseDisplayFresh=P0_Boot.regionSeen.vals[0];''')+leaf
 body=re.sub(r'formalProtocol=0;if\(P0_Boot.regionSeen.vals\[4\]==1.*?formalProtocol=0;\}if\(metaSeen.vals\[0\]==1.*?formalProtocol=0;\}', '', body)
 for decl in decls+['int u33valid;','int u33color;','int u33font;']:
  var=re.search(r'int (\w+)',decl)[1]
  if len(re.findall(r'\b'+re.escape(var)+r'\b',body))<=1:body=body.replace(decl,'')
 methods.append(f'void HMI33Field{ix}(){{'+body.replace('u33text','P0_Boot.displayScratch.txt')+'}')
# Direct event calls, never one user function calling another user function.
formatter+=choose([(j,f'HMI33Field{order[j]}();') for j,leaf in leaves],Q+'[14]')
formatter+=sub('''@Q[14]=@Q[14]+1;if(@Q[14]>=u33end){@Q[14]=u33start;}@Q[40]=@Q[40]+1;}}''')
# per-page methods advance their cursor

# Chunk native theme property updates; page entry itself stays constant time.
theme=''
for p in model['Pages']:
 name=p['Name'];pidx=p['Id']
 theme+=f'if(sys_pid=={pidx} && {Q}[17]==1){{\n'
 for j,c in enumerate(p['Controls']):
  key=name+'.'+c['Name'];tp=c['Type'].split('.')[-1]
  lines=[]
  if tp in ['TextCtrl','ButtonCtrl','ToggleButtonCtrl']:
   if tp=='TextCtrl':
    lines += [f'{key}.bgColor=-1;if(P0_Boot.themeMode.val==1){{{key}.bgColor=-16051162;}}']
    lines += [f'if({key}.fontColor==-15438781 || {key}.fontColor==-10624850){{{key}.fontColor=P0_Boot.themeGood.val;}}else if({key}.fontColor==-4971752 || {key}.fontColor==-39558){{{key}.fontColor=P0_Boot.themeFault.val;}}else{{{key}.fontColor=P0_Boot.themeInk.val;}}']
   else:
    old=p['Events'][0]['Code'];pat=re.escape(key)+r'\.(?:bgImg|bgImg2|disableBgImg|fontColor|bgColor|bgColor2)=[^;]+;'
    # Resource-aware assignments from each theme branch are preserved.
    themefile=R/'事件31'/f'{name}_主题.txt'
    ts=themefile.read_text(encoding='utf-8-sig')
    branches=ts.split('else{',1)
    sets=[re.findall(pat,b) for b in branches]
    # Explicit image names avoid branch parsing through nested color conditions.
    for th,cond in [('Day','==0'),('Night','!=0')]:
     kind='danger' if c['Name']=='estop' else 'primary' if c['Name'] in ['authorize','servoEntry','themeToggle'] else 'nav'
     w,h=c['Properties']['Width'],c['Properties']['Height']
     props=[('bgImg','normal'),('pbgImg','pressed'),('dbgImg','disabled')]
     assignments=[]
     for prop,state in props:
      token=f'user.image.{th}_{kind}_{state}_{w}x{h}'
      if token in resources:assignments.append(f'{key}.{prop}={token};')
     assignments.append(f'{key}.fontColor='+('-1;' if kind in ['primary','danger'] else 'P0_Boot.themeInk.val;'))
     lines.append(f'if(P0_Boot.themeMode.val{cond}){{'+''.join(assignments)+'}')
  if tp=='SliderCtrl':lines += [f'{key}.tbColor=P0_Boot.themeGood.val;']
  if lines:theme+=f'if({Q}[16]<={j} && {Q}[16]+6>{j}){{'+''.join(lines)+'}\n'
 theme+=f'{Q}[16]={Q}[16]+6;if({Q}[16]>={len(p["Controls"])}){{{Q}[17]=0;}}}}\n'
for p in model['Pages']:
 name=p['Name'];page=p['Id']
 # balanced parser retains exactly this page theme statement
 parsed=Parser(theme).all()
 nodes=parsed[1]
 page_node=nodes[page]
 def emit(n):
  if n is None:return ''
  if n[0]=='stmt':return n[1]
  if n[0]=='block':return '{'+''.join(emit(x) for x in n[1])+'}'
  return n[0]+n[1]+emit(n[2])+('else '+emit(n[3]) if n[3] else '')
 methods.append(f'void HMI33Theme{page}()'+emit(page_node[2]))
 formatter+=f'if(sys_pid=={page} && {Q}[17]==1){{HMI33Theme{page}();}}'
put('HMI33自定义函数.txt','\n'.join(methods))

formatter=re.sub(r'formalProtocol=0;if\(P0_Boot.regionSeen.vals\[4\]==1.*?formalProtocol=0;\}if\(metaSeen.vals\[0\]==1.*?formalProtocol=0;\}', '', formatter)
formatter=formatter.replace('u33text','P0_Boot.displayScratch.txt')
# Immutable grouping gates and live control flags are not display history.
formatter=formatter.replace('P0_Boot.dStatus.vals[11]',S+'[11]')
formatter=formatter.replace('P0_Boot.dStatus.vals[8]',S+'[8]')
formatter=formatter.replace('P0_Boot.dStatus.vals[12]',S+'[12]').replace('P0_Boot.dStatus.vals[13]',S+'[13]')
methodsText=(R/'HMI33自定义函数.txt').read_text(encoding='utf-8-sig')
for n in [8,11,12,13]:methodsText=methodsText.replace(f'P0_Boot.dStatus.vals[{n}]',S+f'[{n}]')
for ctl in model['Pages'][0]['Controls']:
 methodsText=re.sub(r'(?<![.\w])'+re.escape(ctl['Name'])+r'\.', 'P0_Boot.'+ctl['Name']+'.', methodsText)
put('HMI33自定义函数.txt',methodsText)
for decl in decls+re.findall(r'int u33\w+;',formatter):
 var=re.search(r'int (\w+)',decl)[1]
 if len(re.findall(r'\b'+re.escape(var)+r'\b',formatter))<=1:formatter=formatter.replace(decl,'')
put('HMI33显示刷新.txt',formatter)

(E/'33号显示字段.json').write_text(json.dumps({'fields':fields,'order':order},ensure_ascii=False,indent=2),encoding='utf8')
for p in model['Pages']:
 name=p['Name'];page=p['Id'];entry=sub('@Q[14]=0;@Q[16]=0;@Q[17]=1;@Q[13]=0-1;\n')
 entry+=f'if(P0_Boot.themeMode.val==0){{{name}.bgImg=user.image.Day_{name};}}else{{{name}.bgImg=user.image.Night_{name};}}\n'
 put(f'事件33/{name}_加载.txt',entry)
put('事件33/主题切换.txt','if(P0_Boot.themeMode.val==0){P0_Boot.themeMode.val=1;}else{P0_Boot.themeMode.val=0;}\n'+(R/'事件33/P1_Main_加载.txt').read_text(encoding='utf-8-sig'))
print('Generated display',len(formatter),'fields',len(fields))
