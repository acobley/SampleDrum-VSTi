# Factory samples

Every `.wav` in this folder ships in the installer as the **Factory samples**
component, and is installed to

    /Users/Shared/SampleDrum/Samples

keeping its sub-folders. This README and anything else is left out. With no
`.wav` here, the installer simply has no samples choice.

That path is `kFactorySampleRoot` in `source/SampleDrumSample.h`, and
`installer/build-installer.sh` refuses to run if the two disagree. **Once a
release has shipped, neither may change** — a project saved referring to the
folder would lose those cells.

## Making a demo project that travels

1. Put the files here, in sub-folders if you like (`Kicks/`, `Hats/`, …).
2. Copy them to `/Users/Shared/SampleDrum/Samples` on your own Mac, with the
   same sub-folders — or build and run the installer, which does it for you.
3. Build the kit by dragging cells **from `/Users/Shared/SampleDrum/Samples`**,
   not from here. A cell remembers the path it was loaded from, and only that
   path is the same on every Mac.

Cells loaded from the factory folder are **not** embedded in the project:
everyone who installs the samples already has them. Any other file on a cell
**is** embedded, so a project can also be sent to someone who has only the
plug-in. See `PORTING-NOTES.md` §10, "Projects that travel".

Only ship samples you have the right to redistribute: the whole repository,
this folder included, is under CC BY-SA 4.0 (see `LICENSE`).
