/* All optional modes use the same isolated system archive and first earned slot.
   Puzzle mode, level and players are applied through their native selectors. */
#define OPTIONAL_LEVEL(id, kind, group, level, label) \
    {{1,id,kind,CATHERINE_DEV_CAN_LOAD,0,1,group,level,-1,-1,label, \
      "Native optional-mode stage using an isolated checkpoint archive."}, \
     "C:/catherine/recomp/timing-audit/paused-checkpoints/catherine-cathedral-full-day8-room-30-slot20.dat", \
     "checkpoints/DDFD13474929B11D71CF974A907CB8C35FD7BB0928B085AF322376D92631B56E.dat", \
     "DDFD13474929B11D71CF974A907CB8C35FD7BB0928B085AF322376D92631B56E"},
#define BABEL_TOWER(level, name) \
    OPTIONAL_LEVEL(20000+level,CATHERINE_DEV_BABEL,1,level,"Babel / Single / " name) \
    OPTIONAL_LEVEL(20100+level,CATHERINE_DEV_BABEL,2,level,"Babel / Pair / " name)
BABEL_TOWER(1,"Altar")
BABEL_TOWER(2,"Menhir")
BABEL_TOWER(3,"Obelisk")
BABEL_TOWER(4,"Axis Mundi")
#undef BABEL_TOWER

#define COLOSSEUM_STAGE(level) \
    OPTIONAL_LEVEL(21000+level,CATHERINE_DEV_COLOSSEUM,1,level,"Colosseum / Stage " #level)
COLOSSEUM_STAGE(1)
COLOSSEUM_STAGE(2)
COLOSSEUM_STAGE(3)
COLOSSEUM_STAGE(4)
COLOSSEUM_STAGE(5)
COLOSSEUM_STAGE(6)
COLOSSEUM_STAGE(7)
COLOSSEUM_STAGE(8)
COLOSSEUM_STAGE(9)
COLOSSEUM_STAGE(10)
COLOSSEUM_STAGE(11)
#undef COLOSSEUM_STAGE

#define RAPUNZEL_STAGE(base, group, name, level) \
    OPTIONAL_LEVEL(base+level,CATHERINE_DEV_RAPUNZEL,group,level,"Rapunzel / " name " / Floor " #level)
#define RAPUNZEL_BOOK(base, group, name) \
    RAPUNZEL_STAGE(base,group,name,1) \
    RAPUNZEL_STAGE(base,group,name,2) \
    RAPUNZEL_STAGE(base,group,name,3) \
    RAPUNZEL_STAGE(base,group,name,4) \
    RAPUNZEL_STAGE(base,group,name,5) \
    RAPUNZEL_STAGE(base,group,name,6) \
    RAPUNZEL_STAGE(base,group,name,7) \
    RAPUNZEL_STAGE(base,group,name,8) \
    RAPUNZEL_STAGE(base,group,name,9) \
    RAPUNZEL_STAGE(base,group,name,10) \
    RAPUNZEL_STAGE(base,group,name,11) \
    RAPUNZEL_STAGE(base,group,name,12) \
    RAPUNZEL_STAGE(base,group,name,13) \
    RAPUNZEL_STAGE(base,group,name,14) \
    RAPUNZEL_STAGE(base,group,name,15) \
    RAPUNZEL_STAGE(base,group,name,16) \
    RAPUNZEL_STAGE(base,group,name,17) \
    RAPUNZEL_STAGE(base,group,name,18) \
    RAPUNZEL_STAGE(base,group,name,19) \
    RAPUNZEL_STAGE(base,group,name,20) \
    RAPUNZEL_STAGE(base,group,name,21) \
    RAPUNZEL_STAGE(base,group,name,22) \
    RAPUNZEL_STAGE(base,group,name,23) \
    RAPUNZEL_STAGE(base,group,name,24) \
    RAPUNZEL_STAGE(base,group,name,25) \
    RAPUNZEL_STAGE(base,group,name,26) \
    RAPUNZEL_STAGE(base,group,name,27) \
    RAPUNZEL_STAGE(base,group,name,28) \
    RAPUNZEL_STAGE(base,group,name,29) \
    RAPUNZEL_STAGE(base,group,name,30) \
    RAPUNZEL_STAGE(base,group,name,31) \
    RAPUNZEL_STAGE(base,group,name,32) \
    RAPUNZEL_STAGE(base,group,name,33) \
    RAPUNZEL_STAGE(base,group,name,34) \
    RAPUNZEL_STAGE(base,group,name,35) \
    RAPUNZEL_STAGE(base,group,name,36) \
    RAPUNZEL_STAGE(base,group,name,37) \
    RAPUNZEL_STAGE(base,group,name,38) \
    RAPUNZEL_STAGE(base,group,name,39) \
    RAPUNZEL_STAGE(base,group,name,40) \
    RAPUNZEL_STAGE(base,group,name,41) \
    RAPUNZEL_STAGE(base,group,name,42) \
    RAPUNZEL_STAGE(base,group,name,43) \
    RAPUNZEL_STAGE(base,group,name,44) \
    RAPUNZEL_STAGE(base,group,name,45) \
    RAPUNZEL_STAGE(base,group,name,46) \
    RAPUNZEL_STAGE(base,group,name,47) \
    RAPUNZEL_STAGE(base,group,name,48) \
    RAPUNZEL_STAGE(base,group,name,49) \
    RAPUNZEL_STAGE(base,group,name,50) \
    RAPUNZEL_STAGE(base,group,name,51) \
    RAPUNZEL_STAGE(base,group,name,52) \
    RAPUNZEL_STAGE(base,group,name,53) \
    RAPUNZEL_STAGE(base,group,name,54) \
    RAPUNZEL_STAGE(base,group,name,55) \
    RAPUNZEL_STAGE(base,group,name,56) \
    RAPUNZEL_STAGE(base,group,name,57) \
    RAPUNZEL_STAGE(base,group,name,58) \
    RAPUNZEL_STAGE(base,group,name,59) \
    RAPUNZEL_STAGE(base,group,name,60) \
    RAPUNZEL_STAGE(base,group,name,61) \
    RAPUNZEL_STAGE(base,group,name,62) \
    RAPUNZEL_STAGE(base,group,name,63) \
    RAPUNZEL_STAGE(base,group,name,64)
RAPUNZEL_BOOK(22000,1,"Classic")
RAPUNZEL_BOOK(22064,2,"Classic Extra")
RAPUNZEL_BOOK(22128,3,"Remix")
RAPUNZEL_BOOK(22192,4,"Remix Extra")
#undef RAPUNZEL_BOOK
#undef RAPUNZEL_STAGE
#define RAPUNZEL_ALTERNATE(base, group, name) \
    OPTIONAL_LEVEL(base+1,CATHERINE_DEV_RAPUNZEL,group,65,"Rapunzel / " name " / Floor 52 (alternate layout)") \
    OPTIONAL_LEVEL(base+2,CATHERINE_DEV_RAPUNZEL,group,66,"Rapunzel / " name " / Floor 56 (alternate layout)") \
    OPTIONAL_LEVEL(base+3,CATHERINE_DEV_RAPUNZEL,group,67,"Rapunzel / " name " / Floor 60 (alternate layout)") \
    OPTIONAL_LEVEL(base+4,CATHERINE_DEV_RAPUNZEL,group,68,"Rapunzel / " name " / Floor 64 (alternate layout)")
RAPUNZEL_ALTERNATE(22300,1,"Classic")
RAPUNZEL_ALTERNATE(22304,2,"Classic Extra")
RAPUNZEL_ALTERNATE(22308,3,"Remix")
RAPUNZEL_ALTERNATE(22312,4,"Remix Extra")
#undef RAPUNZEL_ALTERNATE
#undef OPTIONAL_LEVEL
