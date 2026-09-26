//------------------------------------------------------------------------
// SampleDrum - edit controller implementation
//------------------------------------------------------------------------

#include "SampleDrumController.h"
#include "SampleDrumDsp.h"
#include "SampleDrumEditor.h"
#include "SampleDrumIDs.h"
#include "SampleDrumState.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SampleDrum {

//------------------------------------------------------------------------
namespace {

/** A RangeParameter that formats its own text, so the host's generic
    editor shows the same number a panel will. */
class SampleDrumParameter : public RangeParameter
{
public:
	SampleDrumParameter (const ParamDef& def)
	// TITLE AND UNITS MUST NOT BE NULL. RangeParameter passes them
	// straight to UString::assign, which dereferences both without a
	// null check. The symptom is not a crash in the plug-in: it is the
	// SDK VALIDATOR segfaulting during EditController::initialize, in
	// the post-build step, which sends you looking at the build system.
	// Empty string, never null.
	: RangeParameter (USTRING (def.title), def.id, USTRING (def.units), def.plainMin, def.plainMax,
	                  def.plainDefault, def.stepCount,
	                  // A column's NOTE is a setting, not a performance
	                  // gesture: automating it mid-song is a way to lose
	                  // hits. Everything else can be automated.
	                  isColumnNoteParam (def.id) ? 0 : ParameterInfo::kCanAutomate,
	                  kRootUnitId, USTRING (def.title))
	, mDef (def)
	{
	}

	void toString (ParamValue normalized, String128 string) const SMTG_OVERRIDE
	{
		char text[64] = {};

		if (mDef.type == ParamType::Bool)
		{
			std::snprintf (text, sizeof (text), "%s", (normalized >= 0.5) ? "On" : "Off");
		}
		else if (mDef.id == kOutputTrim)
		{
			// The sign is explicit: a trim reading "3.0 dB" when it is
			// boosting and "-3.0 dB" when it is cutting makes the
			// centre hard to find at a glance.
			std::snprintf (text, sizeof (text), "%+.1f", mDef.toPlain (normalized));
		}
		else if (isColumnNoteParam (mDef.id))
		{
			// "36 C1": the number first, so the host's default text-to-value
			// reading (and the validator's round trip) finds it.
			formatNote (noteFromNormalized (normalized), text, static_cast<int> (sizeof (text)));
		}
		else if (isCellLevelParam (mDef.id))
		{
			// A NUMBER EVEN AT THE BOTTOM, where the cell is silent: the
			// validator reads this text back through the default
			// string-to-value conversion, and "off" would not survive the
			// trip. The panel says "off" instead - see the editor.
			std::snprintf (text, sizeof (text), "%.1f", mDef.toPlain (normalized));
		}
		else
		{
			std::snprintf (text, sizeof (text), "%.2f", mDef.toPlain (normalized));
		}

		UString (string, str16BufferSize (String128)).assign (USTRING (text));
	}

private:
	ParamDef mDef;
};

} // anonymous namespace

//------------------------------------------------------------------------
SampleDrumController::SampleDrumController ()
{
	// RANDOM, not the object's address: two instances in two processes
	// (a host that sandboxes plug-ins) can have the same address. The
	// clock is mixed in for a random_device that is deterministic.
	std::random_device device;
	mInstanceId = (static_cast<std::uint64_t> (device ()) << 32) ^ device ()
	              ^ static_cast<std::uint64_t> (
	                    std::chrono::steady_clock::now ().time_since_epoch ().count ());
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::initialize (FUnknown* context)
{
	const tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	for (int i = 0; i < kNumParams; ++i)
		parameters.addParameter (new SampleDrumParameter (kParams[i]));

	// Standard VST3 bypass, id 1000 and not part of the table - see
	// SampleDrumParams.h. STR16 ("") for units, never nullptr.
	parameters.addParameter (STR16 ("Bypass"), STR16 (""), 1, 0,
	                         ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::terminate ()
{
	mEditors.clear ();
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::setComponentState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	// THE PROCESSOR'S OWN READER, so the two halves cannot disagree about
	// the layout. It starts from defaults, so a short stream cannot leave
	// the previous project's values - or kit - on the panel.
	StreamState in;
	if (!readState (streamer, in))
		return kResultFalse;

	for (int i = 0; i < kNumParams; ++i)
		setParamNormalized (kParams[i].id, in.params[i]);
	setParamNormalized (kBypass, in.bypass ? 1.0 : 0.0);

	for (int cell = 0; cell < kCellCount; ++cell)
	{
		// The status comes from the processor's report for THIS path, if
		// it has arrived yet - see cellStatus().
		mPaths[cell] = in.paths[cell];
		refreshCellInEditors (cell);
	}

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::setState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::getState (IBStream* /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::setParamNormalized (ParamID tag, ParamValue value)
{
	const tresult result = EditControllerEx1::setParamNormalized (tag, value);
	if (result != kResultOk)
		return result;

	for (auto* editor : mEditors)
		editor->updateControl (tag, value);

	return result;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API SampleDrumController::createView (FIDString name)
{
	if (name && FIDStringsEqual (name, ViewType::kEditor))
		return new SampleDrumEditor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SampleDrumController::notify (IMessage* message)
{
	if (message == nullptr)
		return kInvalidArgument;

	const FIDString id = message->getMessageID ();

	if (FIDStringsEqual (id, kSampleDrumCellStatusMessage))
	{
		int64 cell = -1;
		int64 status = 0;
		if (message->getAttributes ()->getInt (kSampleDrumCellIndexAttribute, cell) == kResultOk
		    && message->getAttributes ()->getInt (kSampleDrumCellStatusAttribute, status) == kResultOk
		    && isCellIndex (static_cast<int> (cell))
		    && status >= 0 && status <= static_cast<int64> (kLastSampleStatus))
		{
			std::string path;
			const void* bytes = nullptr;
			uint32 size = 0;
			if (message->getAttributes ()->getBinary (kSampleDrumCellPathAttribute, bytes, size) == kResultOk
			    && bytes != nullptr && size > 0)
				path.assign (static_cast<const char*> (bytes), size);

			mReportedPaths[cell] = path;
			mReported[cell] = static_cast<SampleStatus> (status);
			refreshCellInEditors (static_cast<int> (cell));
		}
		return kResultOk;
	}

	if (FIDStringsEqual (id, kSampleDrumActivityDataMessage))
	{
		const void* hits = nullptr;
		const void* sounding = nullptr;
		uint32 hitsSize = 0, soundingSize = 0;
		if (message->getAttributes ()->getBinary (kSampleDrumHitsAttribute, hits, hitsSize) == kResultOk
		    && message->getAttributes ()->getBinary (kSampleDrumSoundingAttribute, sounding, soundingSize) == kResultOk
		    && hitsSize == sizeof (std::uint32_t) * kCellCount
		    && soundingSize == sizeof (std::uint8_t) * kCellCount)
		{
			for (auto* editor : mEditors)
				editor->setActivity (static_cast<const std::uint32_t*> (hits),
				                     static_cast<const std::uint8_t*> (sounding));
		}
		return kResultOk;
	}

	return EditControllerEx1::notify (message);
}

//------------------------------------------------------------------------
const std::string& SampleDrumController::cellPath (int cell) const
{
	static const std::string kNone;
	return isCellIndex (cell) ? mPaths[cell] : kNone;
}

//------------------------------------------------------------------------
SampleStatus SampleDrumController::cellStatus (int cell) const
{
	if (!isCellIndex (cell) || mPaths[cell].empty ())
		return SampleStatus::Empty;

	// Only a report ABOUT THIS FILE is believed; otherwise the guess.
	return (mReportedPaths[cell] == mPaths[cell]) ? mReported[cell] : SampleStatus::Loaded;
}

//------------------------------------------------------------------------
void SampleDrumController::setCellPath (int cell, const std::string& path)
{
	setCellPath (cell, path, -1);
}

//------------------------------------------------------------------------
void SampleDrumController::setCellPath (int cell, const std::string& path, int carriedFrom)
{
	if (!isCellIndex (cell))
		return;

	mPaths[cell] = path;
	refreshCellInEditors (cell);

	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kSampleDrumCellMessage);
		message->getAttributes ()->setInt (kSampleDrumCellIndexAttribute, cell);

		// ABSENT means cleared - an empty binary is not sent.
		if (!path.empty ())
			message->getAttributes ()->setBinary (kSampleDrumCellPathAttribute, path.data (),
			                                      static_cast<uint32> (path.size ()));

		// Only for a drag between cells - see kSampleDrumCellFromAttribute.
		if (isCellIndex (carriedFrom))
			message->getAttributes ()->setInt (kSampleDrumCellFromAttribute, carriedFrom);

		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void SampleDrumController::writeParam (ParamID tag, double normalized)
{
	const double clamped = std::min (1.0, std::max (0.0, normalized));
	if (getParamNormalized (tag) == clamped)
		return;

	beginEdit (tag);
	setParamNormalized (tag, clamped);
	performEdit (tag, clamped);
	endEdit (tag);
}

//------------------------------------------------------------------------
void SampleDrumController::moveCell (int from, int to, bool copy)
{
	if (!isCellIndex (from) || !isCellIndex (to) || from == to)
		return;

	// NOTHING TO MOVE: an empty cell cannot be picked up - the view
	// refuses - so this is against a source emptied while in flight.
	const std::string fromPath = mPaths[from];
	if (fromPath.empty ())
		return;
	const std::string toPath = mPaths[to];

	// THE LEVELS BEFORE THE PATHS, so the path change is the last thing
	// and every editor it refreshes sees a cell that has finished moving.
	// READ BOTH BEFORE WRITING EITHER, or a swap reads back the value it
	// has just written and both cells end up the same.
	const ParamID fromLevel = cellLevelParam (from);
	const ParamID toLevel   = cellLevelParam (to);
	const double sourceLevel = getParamNormalized (fromLevel);
	const double targetLevel = getParamNormalized (toLevel);
	writeParam (toLevel, sourceLevel);
	if (!copy)
		writeParam (fromLevel, targetLevel);

	// EACH HALF NAMES THE CELL ITS FILE CAME FROM, so the processor
	// carries the embedded copy rather than re-reading a path that may
	// not exist on this machine. The swap's second half names `to`,
	// which the first half has already replaced - CellFiles keeps each
	// cell's previous file for exactly that. A voice still ringing on
	// either cell is ended by the DSP's syncCells, as for any replacement.
	setCellPath (to, fromPath, from);
	if (!copy)
		setCellPath (from, toPath, to);     // the SWAP - may be empty, which empties it
}

//------------------------------------------------------------------------
void SampleDrumController::clearCell (int cell)
{
	if (!isCellIndex (cell) || mPaths[cell].empty ())
		return;

	setCellPath (cell, std::string (), -1);
}

//------------------------------------------------------------------------
void SampleDrumController::auditionCell (int cell)
{
	if (!isCellIndex (cell))
		return;

	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kSampleDrumAuditionMessage);
		message->getAttributes ()->setInt (kSampleDrumCellIndexAttribute, cell);
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void SampleDrumController::requestActivity ()
{
	if (auto* message = allocateMessage ())
	{
		FReleaser releaser (message);
		message->setMessageID (kSampleDrumActivityRequestMessage);
		sendMessage (message);
	}
}

//------------------------------------------------------------------------
void SampleDrumController::refreshCellInEditors (int cell)
{
	for (auto* editor : mEditors)
		editor->refreshCell (cell);
}

//------------------------------------------------------------------------
void SampleDrumController::editorAttached (EditorView* editor)
{
	if (auto* e = dynamic_cast<SampleDrumEditor*> (editor))
	{
		if (std::find (mEditors.begin (), mEditors.end (), e) == mEditors.end ())
			mEditors.push_back (e);
	}
}

//------------------------------------------------------------------------
void SampleDrumController::editorRemoved (EditorView* editor)
{
	editorDestroyed (editor);
}

//------------------------------------------------------------------------
void SampleDrumController::editorDestroyed (EditorView* editor)
{
	// DO NOT dynamic_cast HERE. EditorView::~EditorView() calls this, and
	// by then the SampleDrumEditor sub-object is already destroyed: the
	// dynamic type is plain EditorView, dynamic_cast yields NULL, and the
	// entry would survive in mEditors as a dangling pointer for the next
	// setParamNormalized to follow. Comparing the UPCAST pointer is well
	// defined at every point in destruction.
	mEditors.erase (std::remove_if (mEditors.begin (), mEditors.end (),
	                                [editor] (SampleDrumEditor* e) {
		                                return static_cast<EditorView*> (e) == editor;
	                                }),
	                mEditors.end ());
}

//------------------------------------------------------------------------
} // namespace SampleDrum
