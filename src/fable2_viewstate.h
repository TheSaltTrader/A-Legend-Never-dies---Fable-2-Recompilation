// When the game last built a WORLD camera projection (16:9 kind, inside a
// region) - the field-of-view hook in patch_hooks.cpp sees every camera and
// tells the kinds apart. The HUD overlay uses it for Ultrawide: a frame with
// a world camera behind it is stretched edge to edge (its projection was
// made for the display), everything else - the title, the main menus, the
// loading map, any 2D screen - keeps 16:9 with bars. The user's rule: the
// front end is always 16:9 and 2D art is never stretched.
#pragma once

namespace fable2 {

// Seconds since the last world-camera build; a large number before the first.
double SecondsSinceWorldCameraBuild();
// Seconds the world camera has been built without a gap longer than a
// quarter second; 0 while there is no such run. Right after a load the
// camera is built only every other frame or so, and switching the
// presenter on every one of those was a re-layout per frame (12 fps for
// 15 s, 2026-09-13): the switch waits for half a second of steady frames.
// True once a loading-map camera has been built more recently than a world
// camera: the presenter shows 16:9 with bars from that frame on.
bool LoadingCameraAfterWorld();
// True while the game is in the world scene (two quick world-camera builds
// enter it; a loading-map camera or a run of title/menu camera builds
// leaves it; a still camera keeps it): the hook projects wide and the
// presenter stretches, together.
bool WorldCameraLive();

}  // namespace fable2
