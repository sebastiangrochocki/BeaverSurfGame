#![no_std]
#![no_main]
#![cfg_attr(test, feature(custom_test_frameworks))]
#![cfg_attr(test, reexport_test_harness_main = "test_main")]
#![cfg_attr(test, test_runner(agb::test_runner::test_runner))]

extern crate alloc;

mod music;
mod render;
mod song;

use agb::{
    include_wav,
    input::{Button, ButtonController},
    sound::mixer::{Frequency, SoundChannel, SoundData},
};
use beaver_sim::{Input, Phase, World, decode_save, encode_save};
use render::{Layers, draw_actors, install_palettes};
use serde::{Deserialize, Serialize};

const ROM_MAGIC: [u8; 32] = *b"BEAVERSURF_GBA_SRAM_V1__________";

static JUMP: SoundData = include_wav!("sfx/jump.wav");
static HIT: SoundData = include_wav!("sfx/hit.wav");
static RING: SoundData = include_wav!("sfx/ring.wav");
static SPLASH: SoundData = include_wav!("sfx/splash.wav");

#[derive(Clone, Copy, Serialize, Deserialize)]
struct CartridgeSave {
    bytes: [u8; 8],
}

#[agb::entry]
fn main(mut gba: agb::Gba) -> ! {
    let mut slots = gba.save.init_sram::<()>(1, ROM_MAGIC).ok();
    let mut world = World::new();
    if let Some(slots) = slots.as_mut() {
        if let Ok(save) = slots.read::<CartridgeSave>(0) {
            if let Some(best) = decode_save(&save.bytes) {
                world.best = best;
            }
        }
    }

    let mut gfx = gba.graphics.get();
    install_palettes(&mut gfx);
    let mut layers = Layers::new();
    let mut mixer = gba.mixer.mixer(Frequency::Hz10512);
    let mut music = music::Music::start();
    let mut buttons = ButtonController::new();
    let mut best_before_run = world.best;

    loop {
        buttons.update();
        let input = Input {
            left: buttons.is_pressed(Button::Left),
            right: buttons.is_pressed(Button::Right),
            jump: buttons.is_pressed(Button::A) || buttons.is_pressed(Button::Up),
            dive: buttons.is_pressed(Button::Down) || buttons.is_pressed(Button::B),
            start: buttons.is_pressed(Button::Start),
        };
        let phase = world.phase;
        let previous_best = world.best;
        let events = world.step(input);
        if phase == Phase::Title && world.phase == Phase::Play {
            best_before_run = previous_best;
        }
        if world.best != previous_best {
            if let Some(slots) = slots.as_mut() {
                let bytes = encode_save(world.best);
                let _ = slots.write(0, &CartridgeSave { bytes }, &());
            }
        }
        if events.jumped {
            let _ = mixer.play_sound(SoundChannel::new(JUMP));
        }
        if events.landed {
            let _ = mixer.play_sound(SoundChannel::new(SPLASH));
        }
        if events.trick {
            let _ = mixer.play_sound(SoundChannel::new(RING));
        }
        if events.wipeout {
            let _ = mixer.play_sound(SoundChannel::new(HIT));
        }
        if events.hit {
            let _ = mixer.play_sound(SoundChannel::new(HIT));
        }
        if events.ring || events.heart {
            let _ = mixer.play_sound(SoundChannel::new(RING));
        }

        layers.scroll(&world);
        let mut frame = gfx.frame();
        layers.show(&mut frame);
        draw_actors(
            &mut frame,
            &world,
            world.phase == Phase::Over && world.score > best_before_run,
        );
        music.tick();
        mixer.frame();
        frame.commit();
    }
}
