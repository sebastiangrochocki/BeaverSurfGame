#ifdef __SDCC
#pragma bank 7
#endif
#include <string.h>
#include "race.h"
#include "surf.h"
#include "shop.h"
RaceState race;
uint8_t race_challenges;
static uint32_t add32(uint32_t a,uint32_t b) {return a>0xffffffffUL-b?0xffffffffUL:a+b;}
static uint8_t next_precision(void) {return (uint8_t)(80+(race_challenges<2?race_challenges:2)*10);}
void race_reset(void) BANKED {
    memset(&race,0,sizeof race);
    race.precision=next_precision();
}
uint8_t race_near(void) BANKED {
    int16_t rival=world.phase==PHASE_BEACH_END?PX(RIVAL_FINISH_X):PX(RIVAL_BEACH_X);
    int16_t x=(int16_t)(world.beaver.x+world.beach_pan);
    if(world.beach_pan>384u*FP)return 0;
    return (uint8_t)((world.phase==PHASE_BEACH_END || (world.phase==PHASE_BEACH_START && world.stage>=2)) && race.state==RACE_IDLE &&
        x>=rival-PX(52) && x<=rival+PX(52) && world.beaver.y>=PX(RIVAL_BEACH_Y-28) && !shop_near_door());
}
static int16_t magnitude(int16_t n) {return n<0?(int16_t)-n:n;}
uint8_t race_beach_blocked(int16_t x,int16_t y) BANKED {
    int16_t dx,dy,old_dx,old_dy;
    int16_t rival=world.phase==PHASE_BEACH_END?PX(RIVAL_FINISH_X):PX(RIVAL_BEACH_X);
    if(world.beach_pan>384u*FP || (world.phase!=PHASE_BEACH_END && (world.phase!=PHASE_BEACH_START || world.stage<2)) ||
       (race.state!=RACE_IDLE && race.state!=RACE_OFFER))return 0;
    /* Stable union of the walking art: 32 native pixels wide, 24 ink rows high.
     * X uses 3:2 rules/native scaling; transparent bottom eight rows stay free.
     * Resolve proposed axes separately so walking along the edge still slides. */
    dx=magnitude((int16_t)(x-rival));
    dy=magnitude((int16_t)(y-PX(RIVAL_BEACH_Y)));
    if(dx>=PX(48) || dy>=PX(24))return 0;
    old_dx=magnitude((int16_t)(world.beaver.x+world.beach_pan-rival));
    old_dy=magnitude((int16_t)(world.beaver.y-PX(RIVAL_BEACH_Y)));
    /* Old saves/test fixtures may start overlapped: allow only outward escape. */
    if(old_dx<PX(48) && old_dy<PX(24) && dx+dy>old_dx+old_dy)return 0;
    return 1;
}
static void accept(void) {
    if(world.score<1000) {race.short_funds=1;return;}
    /* The normal exit starts the next crossing directly. A finish-beach duel
     * instead stages that upcoming crossing at its race starting line. */
    if(world.phase==PHASE_BEACH_END)world_next_stage_beach();
    race.precision=next_precision();race.ai_accuracy=0;
    if(race_challenges<255)race_challenges++;
    if(world.score>world.best)world.best=world.score;
    race.wallet=world.score-1000;world.score=0;
    race.saved_health=world.hearts;race.state=RACE_COUNTDOWN;race.timer=180;
    world.beach_pan=0;world.beaver.x=BEACH_START_X;world.beaver.y=HORIZON;
    world.beaver.vx=world.beaver.vy=0;world.beaver.airborne=world.beaver.flip=0;
    world.facing_left=shop_direction=0;
    world.beaver.lane=world.beaver.lane_to=LANE_TOP;
    memset(&race.rival,0,sizeof race.rival);
    race.rival.beaver=world.beaver;race.rival.beaver.y=FRONT_HORIZON;
    race.rival.beaver.lane=race.rival.beaver.lane_to=LANE_BOTTOM;
    race.rival.speed=SCROLL_SPEED;race.rival.meter=255;
    surf_reset();
}
uint8_t race_beach(uint8_t input,Events *events) BANKED {
    uint8_t edge=(uint8_t)(input & (uint8_t)~race.held);
    race.held=input;
    if(race.state==RACE_OFFER) {
        world.tick++;world.beaver.vx=world.beaver.vy=0;
        if(edge&INPUT_LEFT)race.yes=1;
        if(edge&INPUT_RIGHT)race.yes=0;
        if(edge&INPUT_DIVE)race.state=RACE_IDLE;
        else if(edge&INPUT_ACTION) {
            if(race.yes) {accept();if(race.state==RACE_COUNTDOWN)events->countdown=3;}
            else race.state=RACE_IDLE;
        }
        return 1;
    }
    if(race.state==RACE_COUNTDOWN) {
        world.tick++;
        if(race.timer)race.timer--;
        if(race.timer==120)events->countdown=2;
        if(race.timer==60)events->countdown=1;
        if(!race.timer) {race.state=RACE_RUNIN;race.timer=30;events->countdown=4;}
        return 1;
    }
    if(race.state==RACE_RUNIN) {
        if(race.timer) {race.timer--;world.tick++;return 1;}
        /* The existing beach walker supplies the automatic, camera-safe run-in. */
        return 0;
    }
    if(race.state==RACE_RESULTS) {
        world.beaver.vx=world.beaver.vy=0;
        if(edge&INPUT_ACTION) {
            if(!race.page)race.page=1;
            else race.state=RACE_DONE;
        }
        return 1;
    }
    if(race_near() && (edge&INPUT_ACTION)) {
        race.state=RACE_OFFER;race.yes=race.short_funds=0;
        world.beaver.vx=world.beaver.vy=0;return 1;
    }
    return 0;
}
void race_begin_surf(void) BANKED {
    if(race.state!=RACE_RUNIN)return;
    race.state=RACE_ACTIVE;race.timer=20;
    race.rival.beaver.x=world.beaver.x;race.rival.beaver.y=front_at(world.scroll+world.beaver.x);
    race.rival.beaver.vx=race.rival.beaver.vy=0;
    race.ai_seen=255;race.ai_think=0;race.ai_wait=12;
}
/* The next target enters only after the previous one has left or been collected.
 * This is deliberately quieter than solo play: 4+4 rider objects + 2 target objects. */
void race_targets(int16_t step) BANKED {
    uint8_t occupied=0;
    (void)step;
    world.heart_present=0;
    memset(world.gull_present,0,sizeof world.gull_present);
    if(pickups[0].flags&PICKUP_PRESENT)occupied=1;
    if(world.course_distance<=256UL*FP || world.course_distance>=course_length-384UL*FP) {
        memset(world.gull_present,0,sizeof world.gull_present);return;
    }
    if(occupied)return;
    if(race.timer) {race.timer--;return;}
    race.timer=12;
    {
        pickups[0].x=PX(264);
        pickups[0].flags=(uint8_t)(PICKUP_PRESENT|((race.sequence&1)?PICKUP_BOTTOM:0)|
            ((race.sequence&7)==6?PICKUP_HIGH:0)|(world.stage>=3?PICKUP_BOB:0));
        pickups[0].y=(int16_t)(lane_surface(world.scroll+PX(264),(race.sequence&1)?1:0)-PX(10));
    }
    race.sequence++;
}
/* A bounded visible-target policy. It reacts to newly visible targets, never
 * offscreen spawn data, and emits only ordinary controls consumed by rider_step. */
uint8_t race_ai(void) BANKED {
    Beaver *b=&race.rival.beaver;
    uint8_t visible=255,lane=b->lane,input=0;
    int16_t x=0,y=0,dx;
    if(pickups[0].flags&PICKUP_PRESENT && pickups[0].x>=0 && pickups[0].x<=PX(240)) {
        visible=race.sequence; x=pickups[0].x;y=pickups[0].y;
        lane=(pickups[0].flags&PICKUP_BOTTOM)?LANE_BOTTOM:LANE_TOP;
    }
    if(visible!=race.ai_seen) {
        race.ai_seen=visible;race.ai_wait=(uint8_t)(12+(race.sequence&3));race.ai_think=0;
    }
    if(race.ai_wait) {race.ai_wait--;race.ai_input&=(INPUT_LEFT|INPUT_RIGHT);return race.ai_input;}
    if(race.ai_think) {
        race.ai_think--;
        /* Requests are taps, not eight-tick holds: release before a mid-air A. */
        race.ai_input&=(uint8_t)~(INPUT_JUMP|INPUT_UP|INPUT_DOWN);
        return race.ai_input;
    }
    race.ai_think=7;
    /* Carry the accuracy budget: 80/90/100 precise plans per hundred.
     * A missed plan coasts for this eight-tick interval instead of perfectly
     * correcting toward a ring, changing lanes or requesting a trick. */
    race.ai_accuracy=(uint8_t)(race.ai_accuracy+race.precision);
    if(race.ai_accuracy<100) {race.ai_input=0;return 0;}
    race.ai_accuracy-=100;
    if(!b->airborne)race.ai_flip=0;
    dx=(int16_t)(x-b->x);
    if(visible!=255) {
        if(dx>PX(12))input|=INPUT_RIGHT;
        else if(dx<PX(-12))input|=INPUT_LEFT;
        {
            if(lane==LANE_BOTTOM && b->lane_to==LANE_TOP) {
                if(!b->airborne)input|=INPUT_JUMP;
                else input|=INPUT_DOWN|INPUT_DIVE;
            }
            if(lane==LANE_TOP && b->lane==LANE_BOTTOM) {
                if(!b->airborne)input|=INPUT_JUMP;
                else if(b->vy<0)input|=INPUT_UP;
            }
            if(y<b->y-PX(24) && dx<PX(80) && !b->airborne)input|=INPUT_JUMP;
        }
    } else {
        int16_t patrol=(int16_t)(world.beaver.x+PX(12));
        if(patrol<PX(128))patrol=PX(128);if(patrol>PX(196))patrol=PX(196);
        if(b->x>patrol+PX(5))input|=INPUT_LEFT;
        else if(b->x<patrol-PX(5))input|=INPUT_RIGHT;
    }
    /* Crest timing and flip requests use the NPC's conservative landing forecast. */
    if(!b->airborne && !(input&INPUT_DIVE)) {
        int32_t wx=world.scroll+b->x;
        uint8_t crest;
        if(b->lane==LANE_BOTTOM) {
            FrontRide motion;front_motion(wx,&motion);
            crest=(uint8_t)(motion.allowed && motion.slope>-FP/2);
        } else crest=sea_is_crest(wx);
        if(crest)input|=INPUT_JUMP;
    } else if(b->airborne && !(input&(INPUT_UP|INPUT_DOWN)) && b->air_time>=3 && !b->flip && !race.ai_flip && b->lane==b->lane_to) {
        input|=INPUT_JUMP;race.ai_flip=1;
    }
    /* Release A between requests; no repeated held-air flips. */
    if(race.ai_input&INPUT_JUMP)input&=(uint8_t)~INPUT_JUMP;
    race.ai_input=input;return input;
}
void race_record(const Events *events,uint8_t rival) BANKED {
    RaceStats *s=rival?&race.rival_stats:&race.player_stats;
    if(!RACING)return;
    s->coins=(uint16_t)(s->coins+events->ring);
    if(events->trick)s->tricks++;
    if(events->hit)s->hits++;
}
void race_finish(void) BANKED {
    if(!RACING)return;
    race.player_score=world.score;
    race.payout=world.score>race.rival.score?2000:world.score==race.rival.score?1000:0;
    world.score=add32(add32(race.wallet,race.player_score),race.payout);
    world.hearts=race.saved_health;world.stun=world.invuln=0;
    race.state=RACE_RESULTS;race.page=0;
    world.beaver.vx=world.beaver.vy=0;
    race.rival.beaver.x=world.beaver.x+PX(48);race.rival.beaver.y=PX(136);
    race.rival.beaver.airborne=race.rival.beaver.flip=0;
}
static void number(char *out,uint32_t n) {
    static const uint32_t places[]={1000000000UL,100000000UL,10000000UL,1000000UL,100000UL,10000UL,1000UL,100UL,10UL,1UL};
    uint8_t i,started=0;
    for(i=0;i<10;i++) {
        uint8_t digit=0;
        while(n>=places[i]) {n-=places[i];digit++;}
        if(digit || started || i==9) {*out++=(char)('0'+digit);started=1;}
    }
    *out=0;
}
static void pair(char *out,const char *a,uint32_t x,const char *b,uint32_t y) {
    strcpy(out,a);number(out+strlen(out),x);strcat(out,b);number(out+strlen(out),y);
}
uint8_t race_hud(char *top,char *bottom,uint8_t footer) BANKED {
    top[0]=bottom[0]=0;
    if(race.state==RACE_OFFER) {
        if(footer) {strcpy(top,race.short_funds?"NOT ENOUGH POINTS":"BET 1000 WIN 2000");strcpy(bottom,race.yes?"YES SELECTED   NO":"YES   NO SELECTED");}
        else {strcpy(top,"RACE FOR POINTS");strcpy(bottom,"LEFT YES  RIGHT NO");}
    } else if(race.state==RACE_COUNTDOWN) {
        if(!footer) {strcpy(top,"READY TO RACE");strcpy(bottom,race.timer>120?"3":race.timer>60?"2":"1");}
        else {
            strcpy(top,"RIVAL AIM ");number(top+10,race.precision);strcat(top," PCT");
            strcpy(bottom,"RINGS FLIPS AND AIR");
        }
    } else if(race.state==RACE_RUNIN) {
        strcpy(top,footer?"MOST POINTS WINS":"GO");strcpy(bottom,footer?"JUMP THEN UP TO CLIMB":"RUN TO THE WATER");
    } else if(RACING) {
        if(!footer) {pair(top,"YOU ",world.score," NPC ",race.rival.score);strcpy(bottom,"JUMP THEN UP OR DOWN");}
        else return 0;
    } else if(race.state==RACE_RESULTS) {
        if(!footer) {
            strcpy(top,race.player_score>race.rival.score?"YOU WIN":race.player_score==race.rival.score?"DRAW":"RIVAL WINS");
            pair(bottom,"YOU ",race.player_score," NPC ",race.rival.score);
        } else if(!race.page) {
            strcpy(top,"PAYOUT ");number(top+7,race.payout);
            strcpy(bottom,"A VIEW RACE STATS");
        } else {
            pair(top,"RING ",race.player_stats.coins," ",race.rival_stats.coins);
            strcat(top," A NEXT");
            pair(bottom,"TRICK ",race.player_stats.tricks>9999?9999:race.player_stats.tricks," ",race.rival_stats.tricks>9999?9999:race.rival_stats.tricks);
            strcat(bottom," HIT ");number(bottom+strlen(bottom),race.player_stats.hits>9999?9999:race.player_stats.hits);
            strcat(bottom," ");number(bottom+strlen(bottom),race.rival_stats.hits>9999?9999:race.rival_stats.hits);
        }
    } else return 0;
    return 1;
}
