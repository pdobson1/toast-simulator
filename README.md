# Toast Simulator

A 16-bit style toasting game, drawn like an Amiga or Mega Drive game: every colour sits on the Mega Drive's 9-bit palette.

Push the lever, watch the needle, and pop the toast inside the yellow brackets for a PERFECT. Earn Toast Points (TP) to buy better toasters, harder breads and (vegan) spreads.

## How to play

- **Space**, a click on the screen or the big button pushes the lever, then pops the toast.
- Every 6 rounds you go up a level: the toast cooks faster and the brackets get tighter.
- You get 3 lives a level. Anything less than a perfect breaks your streak and costs a life. From level 7, a miss also sends you back to 0/6.
- Clear level 10 to finish. Your score comes from perfects (harder breads score more), level clears, lives left and your best streak.
- TP and upgrades carry over from run to run. Your top 5 scores are kept.
- A day passes every four minutes, and a black cat drops by now and then. Keep an eye on your toast.

## Running it

It's a single file with no build step: open `index.html` in a browser. It works on desktop and phones. Progress is saved in the browser's local storage. The two fonts (Press Start 2P and VT323) load from Google Fonts, with fallbacks when offline.

## Playing online

The game is published with GitHub Pages at https://pdobson1.github.io/toast-simulator/, straight from the `main` branch. Every push to `main` redeploys it.

## Mega Drive version

The `megadrive/` folder is the same game as a real Sega Mega Drive ROM, written in C. See [megadrive/README.md](megadrive/README.md). It's playable in the browser at `megadrive/` once published.
