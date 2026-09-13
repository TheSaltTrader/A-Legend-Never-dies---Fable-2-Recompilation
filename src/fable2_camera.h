// The camera's field of view.
//
// The game keeps its vertical field of view as a single constant, in radians,
// at guest address 0x82101034 (0.75 rad short of nothing - it reads 1.0471976,
// which is 60 degrees). It is read every frame and the game never writes it,
// so one write into guest memory holds until the next and the picture widens
// or narrows on the following frame. Found by poking the running game: of the
// three copies of that constant in the image, this is the one whose change
// moved the whole picture (2026-09-13).
//
// This is not a midasm hook: there is nothing to intercept, only a constant to
// set. ApplyFieldOfView writes it, and is called at startup with the saved
// value and again whenever the slider moves.
#pragma once

namespace fable2 {

// The game's own vertical field of view, and the slider's bounds.
constexpr int kFovDefaultDegrees = 60;
constexpr int kFovMinDegrees = 50;
constexpr int kFovMaxDegrees = 100;

// Write `degrees` (vertical) into the game's field-of-view constant. Clamped to
// the range above; a value equal to the default writes the same bytes the game
// shipped, so it is always safe to call. Does nothing if guest memory is not
// mapped yet. Returns the value actually written.
int ApplyFieldOfView(int degrees);

}  // namespace fable2
