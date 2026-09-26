//------------------------------------------------------------------------
// SampleDrum - class UIDs and message identifiers
//
// These UIDs were generated fresh for this plug-in from os.urandom.
// NEVER CHANGE THEM once a build has been shipped: hosts store them in
// the project file, so a changed UID means every existing session
// silently loses the plug-in. The same is true of the four-character
// codes in resource/au-info.plist and of both bundle identifiers in
// CMakeLists.txt.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SampleDrum {

static const Steinberg::FUID kSampleDrumProcessorUID  (0xAFAD783D, 0xFCC97C1E, 0x28EF80EF, 0xD4DC5FCF);
static const Steinberg::FUID kSampleDrumControllerUID (0xDBD88CFF, 0x07124209, 0x6131B114, 0x20CECD1D);

// The plug-in category is declared once, in SampleDrumEntry.cpp, using the
// SDK's own PlugType::kInstrumentDrum - not repeated as a string here.

//------------------------------------------------------------------------
// Processor <-> controller messages
//
// THE RULE THAT GOVERNS ALL OF THEM: every message travels on the UI
// THREAD. sendMessage from process() returns success and is then silently
// discarded by the host's connection proxy - it does nothing and tells
// you nothing. So nothing here is sent from process(): the processor
// sends from setActive, setState and notify, and the controller from the
// editor's handlers and timer, all of which are the UI thread.
//
// Why messages at all, when this project's rule is that anything that is
// a number is a parameter: a FILE PATH is not a number, and 32 cells of
// "what is playing" read by a panel at 30 frames a second would be 32
// parameters thrashing the host's automation queue for something only
// the panel wants. Both follow Project6's worked pattern.
//
// The limitation to know: if a host never connects the two components, a
// message is never delivered and nothing says so. A drop then reaches the
// panel but not the saved project. That is true of every VST3 message.
//------------------------------------------------------------------------

/** Controller -> processor, from a drop on a cell (or an Option-click
    that clears one): which cell, and the path of the file now in it.

    The path travels as BINARY - UTF-8 bytes with an explicit length - so
    nothing converts it to UTF-16 and back. An ABSENT path attribute means
    the cell was cleared. */
static const char* const kSampleDrumCellMessage       = "SampleDrumCell";
static const char* const kSampleDrumCellIndexAttribute = "Cell";
static const char* const kSampleDrumCellPathAttribute  = "Path";

/** OPTIONAL, on the message above: the cell this file came from, when a
    cell was dragged onto another rather than a file dropped from the
    Finder. It is what lets the file's EMBEDDED copy travel with it - on a
    machine that only has the project, the embedded copy is the only copy
    there is. Absent for a drop from outside, which reads the disk afresh.
    As Project6's kProject6SlotFromAttribute. */
static const char* const kSampleDrumCellFromAttribute  = "From";

/** Processor -> controller: how a cell's file actually read, as a
    SampleStatus.

    Sent after every load (notify, setState) and for EVERY cell from
    setActive - the only thing that makes a panel opened later, or a
    project restored before the components were connected, show the truth
    rather than an optimistic guess. A cell that takes the drop, shows the
    name and is silent when hit is the failure this exists to prevent.

    IT CARRIES THE PATH IT IS ABOUT (kSampleDrumCellPathAttribute, absent
    for an empty cell). The processor's setState and the controller's
    setComponentState can arrive in either order, and a status for one
    file must never be pinned to another: the controller only believes a
    status whose path matches the path it is showing. */
static const char* const kSampleDrumCellStatusMessage   = "SampleDrumCellStatus";
static const char* const kSampleDrumCellStatusAttribute = "Status";

/** Controller -> processor, from a click on a loaded cell: play it now.
    Handed to the audio thread through an atomic bit mask, so the click
    is heard at the start of the next block. */
static const char* const kSampleDrumAuditionMessage = "SampleDrumAudition";

/** The panel's lamps, as a REQUEST AND A REPLY, both on the UI thread:
    the editor's timer asks, and the processor answers with every cell at
    once. The reply carries two binaries: kCellCount uint32 hit counters
    (a cell lights when its count MOVES, so a hit shorter than one timer
    tick still flashes) and kCellCount bytes, 1 while a cell sounds. */
static const char* const kSampleDrumActivityRequestMessage = "SampleDrumActivityRequest";
static const char* const kSampleDrumActivityDataMessage    = "SampleDrumActivityData";
static const char* const kSampleDrumHitsAttribute          = "Hits";
static const char* const kSampleDrumSoundingAttribute      = "Sounding";

//------------------------------------------------------------------------
} // namespace SampleDrum
