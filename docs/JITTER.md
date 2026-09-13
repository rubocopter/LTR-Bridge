# Projection-jitter research

Status: **backend requirement verified; legacy injection strategy experiment-pending**.

Correct temporal Super Resolution needs the renderer to produce subpixel-varied samples over time. Passing a non-zero jitter number to the backend is not enough if the rendered scene was never generated with that projection offset.

## Evidence from current feeder experiments

DLSS5-Feeder's normal legacy contract uses DLAA at render size = output size with zero jitter because it observes a finished game frame and cannot change the game's camera projection.

Its experimental synthetic-jitter mode shifts a later downsample grid and reports that offset to DLSS. Upstream explicitly documents why this is not real game-render jitter:

- the game already rendered every native pixel;
- no renderer work is saved;
- the jittered samples all derive from the same already-rendered frame;
- estimated motion vectors become a much larger quality limitation in SR than in DLAA.

This is useful negative evidence: post-process sampling jitter must not be presented as equivalent to projection jitter.

## Where jitter must enter

For a real integration, the adapter or game-specific provider must perturb the projection used for scene rendering before geometry is rasterized, while keeping the backend informed of the exact corresponding offset.

The likely injection point is the projection matrix or equivalent camera constants. Legacy engines may upload that matrix through fixed-function transforms, vertex-shader constants, engine constant buffers, or multiple passes with separate projections.

## Content that must be classified

Not every draw should necessarily inherit the same jitter policy:

- world geometry;
- first-person weapon/hands;
- shadows/reflections that use other projections;
- particles and transparencies;
- HUD/UI;
- video/cinematics;
- each VR eye.

Jittering a screen-space HUD or an unrelated shadow projection can create instability even when the main world projection is correct.

## Sequence and convention

The contract should record jitter in render-pixel units with a declared sign/origin convention. Halton-style subpixel sequences are a reasonable controlled-test choice, but the sequence itself is less important than ensuring the renderer, motion vectors and backend use one consistent convention.

## VR

VR makes projection modification higher risk. Each eye has its own runtime-derived projection and predicted pose. A future stereo experiment must establish whether jitter can be applied to the eye projections without upsetting runtime composition, motion reprojection or binocular stability. Until that is headset-validated, VR jitter remains an experiment rather than an assumed extension of flat-screen TAA.
