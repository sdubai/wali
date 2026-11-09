#include "ps2_pad.h"
#include "ps2_servo_control.h"
#include <cassert>
#include <cstdio>
int main() {
    // Real PS2 face-button bit positions and mirrored eye endpoints.
    assert(Ps2ButtonTarget(0x8000, PS2_SQUARE, PS2_CROSS, 125, 110, 140) == 110);
    assert(Ps2ButtonTarget(0x4000, PS2_SQUARE, PS2_CROSS, 125, 110, 140) == 140);
    assert(Ps2ButtonTarget(0xc000, PS2_SQUARE, PS2_CROSS, 125, 110, 140) == 125);
    assert(Ps2ButtonTarget(0, PS2_SQUARE, PS2_CROSS, 125, 110, 140) == 125);
    assert(Ps2ButtonTarget(0x1000, PS2_TRIANGLE, PS2_CIRCLE, 45, 60, 30) == 60);
    assert(Ps2ButtonTarget(0x2000, PS2_TRIANGLE, PS2_CIRCLE, 45, 60, 30) == 30);
    assert(Ps2ButtonTarget(0x3000, PS2_TRIANGLE, PS2_CIRCLE, 45, 60, 30) == 45);
    assert((PS2_SERVO_BUTTONS & 0xf000) == 0xf000);
    for (const auto endpoints : {std::pair<int,int>{110,140}, {60,30}}) {
        Ps2ServoRamp eye;
        int angle = endpoints.second;
        const int minimum = std::min(endpoints.first, endpoints.second);
        const int maximum = std::max(endpoints.first, endpoints.second);
        for (int target : {endpoints.first, endpoints.second}) {
            for (int i=0;i<1000 && angle!=target;i++) {
                const int next = eye.Advance(angle, target, 0.04f, 12);
                assert(next >= minimum && next <= maximum);
                assert(std::abs(next-angle) <= 1);
                angle = next;
            }
            assert(angle == target);
        }
    }
    Ps2PitchSelector pitch;
    assert(pitch.Update(1) && pitch.level == 1);
    for (int i=0;i<100;i++) assert(!pitch.Update(1) && pitch.level==1);
    pitch.Update(0); assert(pitch.Update(-1) && pitch.level==0);
    for (int i=0;i<100;i++) assert(!pitch.Update(-1) && pitch.level==0);
    pitch.Update(0); assert(pitch.Update(-1) && pitch.level==-1);
    pitch.Update(0); assert(!pitch.Update(-1) && pitch.level==-1);
    pitch.Update(0); assert(pitch.Update(1) && pitch.level==0);
    // Accumulated sub-degree movement must reach both extremes with small
    // command steps, including reversal and short/long poll intervals.
    for (float dt : {0.02f,0.04f,0.05f}) {
        Ps2ServoRamp ramp; int current=108;
        for (int target : {170,10,108,115,145,70,150}) {
            bool reached=false;
            for(int i=0;i<2000;i++) {
                int next=ramp.Advance(current,target,dt,25);
                assert(next>=10 && next<=170);
                assert(std::abs(next-current)<=3);
                current=next;
                if(current==target){ reached=true; break; }
            }
            assert(reached);
        }
        ramp.Reset(current);
        for(int i=0;i<100;i++) assert(ramp.Advance(current,current,dt,25)==current);
        // An external command re-synchronizes the ramp at the new position.
        assert(std::abs(ramp.Advance(90,170,dt,25)-90)<=1);
    }
    int l=0,r=0;
    Ps2TrackSpeeds(PS2_L1|0x10,100,l,r); assert(l==100 && r==100);
    Ps2TrackSpeeds(PS2_R2,100,l,r); assert(l==0 && r==0);
    Ps2StickSpeeds(128,0,100,l,r); assert(l==100 && r==100);
    puts("PASS: pitch detents/hold, motion convergence/limits, external reset, chassis compatibility");
}
