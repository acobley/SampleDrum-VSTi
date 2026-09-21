//------------------------------------------------------------------------
// SampleDrum - the WAV reader, implementation
//
// SDK-free. Every multi-byte value is assembled from bytes rather than
// cast from the buffer: WAV is little-endian and a struct cast would be
// both unaligned and wrong on a big-endian machine.
//------------------------------------------------------------------------

#include "SampleDrumSample.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace SampleDrum {

namespace {

//------------------------------------------------------------------------
uint16_t readU16 (const unsigned char* p)
{
	return static_cast<uint16_t> (p[0] | (p[1] << 8));
}

uint32_t readU32 (const unsigned char* p)
{
	return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8)
	       | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24);
}

bool tagIs (const unsigned char* p, const char* tag)
{
	return p[0] == static_cast<unsigned char> (tag[0])
	       && p[1] == static_cast<unsigned char> (tag[1])
	       && p[2] == static_cast<unsigned char> (tag[2])
	       && p[3] == static_cast<unsigned char> (tag[3]);
}

constexpr uint16_t kFormatPcm        = 0x0001;
constexpr uint16_t kFormatFloat      = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

//------------------------------------------------------------------------
/** One sample, converted to -1..1.

    The divisors are the NEGATIVE full scale: a 16-bit file's -32768
    becomes exactly -1 and +32767 becomes 0.999969. Dividing by 32767
    would push -32768 to -1.000031 and clip a file already at full
    scale. */
float decodeSample (const unsigned char* p, uint16_t format, uint16_t bits)
{
	if (format == kFormatFloat)
	{
		if (bits == 32)
		{
			const uint32_t bits32 = readU32 (p);
			float value = 0.f;
			std::memcpy (&value, &bits32, sizeof (value));
			return value;
		}

		uint64_t bits64 = 0;
		for (int i = 7; i >= 0; --i)
			bits64 = (bits64 << 8) | p[i];
		double value = 0.0;
		std::memcpy (&value, &bits64, sizeof (value));
		return static_cast<float> (value);
	}

	switch (bits)
	{
		case 8:
			// 8-bit wav is UNSIGNED, alone among the PCM depths. Silence
			// is 128, and treating it as signed turns a quiet file into a
			// loud square wave.
			return (static_cast<int> (p[0]) - 128) / 128.f;

		case 16:
			return static_cast<int16_t> (readU16 (p)) / 32768.f;

		case 24:
		{
			const int32_t value = static_cast<int32_t> (
				(static_cast<uint32_t> (p[0]) << 8) | (static_cast<uint32_t> (p[1]) << 16)
				| (static_cast<uint32_t> (p[2]) << 24));
			return (value >> 8) / 8388608.f;
		}

		case 32:
			return static_cast<int32_t> (readU32 (p)) / 2147483648.f;

		default:
			return 0.f;
	}
}

//------------------------------------------------------------------------
bool endsWithNoCase (const std::string& text, const std::string& suffix)
{
	if (suffix.size () > text.size ())
		return false;

	return std::equal (suffix.rbegin (), suffix.rend (), text.rbegin (),
	                   [] (char a, char b)
	                   {
		                   return std::tolower (static_cast<unsigned char> (a))
		                          == std::tolower (static_cast<unsigned char> (b));
	                   });
}

int hexDigit (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/** The first line, trimmed. A text drag usually carries a trailing
    newline, and a drag of several files arrives as several lines. */
std::string firstLine (const std::string& text)
{
	const std::size_t end = text.find_first_of ("\r\n");
	const std::string line = (end == std::string::npos) ? text : text.substr (0, end);

	const std::size_t from = line.find_first_not_of (" \t");
	if (from == std::string::npos)
		return std::string ();
	const std::size_t to = line.find_last_not_of (" \t");

	return line.substr (from, to - from + 1);
}

/** %20 -> space. A per-cent that is not an escape is left alone:
    "100%.wav" is a legal filename. */
std::string percentDecode (const std::string& text)
{
	std::string out;
	out.reserve (text.size ());

	for (std::size_t i = 0; i < text.size (); ++i)
	{
		if (text[i] == '%' && i + 2 < text.size ())
		{
			const int high = hexDigit (text[i + 1]);
			const int low  = hexDigit (text[i + 2]);
			if (high >= 0 && low >= 0)
			{
				out.push_back (static_cast<char> (high * 16 + low));
				i += 2;
				continue;
			}
		}
		out.push_back (text[i]);
	}

	return out;
}

} // namespace

//------------------------------------------------------------------------
const char* sampleStatusText (SampleStatus status)
{
	switch (status)
	{
		case SampleStatus::Empty:             return "empty";
		case SampleStatus::Loaded:            return "loaded";
		case SampleStatus::Unreadable:        return "could not be read";
		case SampleStatus::NotWave:           return "not a WAV file";
		case SampleStatus::UnsupportedFormat: return "unsupported WAV format";
		case SampleStatus::TooLong:           return "longer than 20 seconds";
		case SampleStatus::NoFrames:          return "no audio in the file";
	}
	return "unknown";
}

//------------------------------------------------------------------------
SampleStatus parseWav (const unsigned char* data, std::size_t size, SampleBuffer& out)
{
	out = SampleBuffer ();

	if (data == nullptr || size < 12)
		return SampleStatus::NotWave;

	// RIFX, the big-endian variant, is not decoded; NotWave is honest.
	if (!tagIs (data, "RIFF") || !tagIs (data + 8, "WAVE"))
		return SampleStatus::NotWave;

	bool     haveFormat = false;
	uint16_t format     = 0;
	uint16_t channels   = 0;
	uint32_t rate       = 0;
	uint16_t bits       = 0;

	const unsigned char* audio = nullptr;
	std::size_t audioBytes = 0;

	std::size_t offset = 12;
	while (offset + 8 <= size)
	{
		const unsigned char* id = data + offset;
		const uint32_t chunkSize = readU32 (data + offset + 4);
		const std::size_t body = offset + 8;

		// A chunk claiming more than the file holds is a TRUNCATED FILE,
		// not a licence to read past the end. What is there is used.
		const std::size_t available = (body < size) ? (size - body) : 0;
		const std::size_t usable = std::min (static_cast<std::size_t> (chunkSize), available);

		if (tagIs (id, "fmt ") && usable >= 16)
		{
			format   = readU16 (data + body);
			channels = readU16 (data + body + 2);
			rate     = readU32 (data + body + 4);
			bits     = readU16 (data + body + 14);

			// WAVE_FORMAT_EXTENSIBLE: the real code is the first two
			// bytes of the SubFormat GUID, 24 bytes into the chunk.
			if (format == kFormatExtensible && usable >= 40)
				format = readU16 (data + body + 24);

			haveFormat = true;
		}
		else if (tagIs (id, "data"))
		{
			audio = data + body;
			audioBytes = usable;
		}

		// WORD-ALIGNED: an odd-length chunk is followed by a pad byte the
		// size does not count. Missing it reads every later chunk id one
		// byte late - and metadata chunks are odd all the time.
		std::size_t advance = 8 + static_cast<std::size_t> (chunkSize);
		if ((chunkSize & 1u) != 0u)
			++advance;
		offset += advance;
	}

	if (!haveFormat || channels == 0 || rate == 0)
		return SampleStatus::NotWave;

	const bool decodablePcm   = (format == kFormatPcm)
	                            && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
	const bool decodableFloat = (format == kFormatFloat) && (bits == 32 || bits == 64);
	if (!decodablePcm && !decodableFloat)
		return SampleStatus::UnsupportedFormat;

	const std::size_t bytesPerSample = bits / 8u;
	const std::size_t frameBytes     = bytesPerSample * channels;

	if (audio == nullptr || audioBytes < frameBytes)
		return SampleStatus::NoFrames;

	const std::size_t frames = audioBytes / frameBytes;

	// In SECONDS, so it means the same at 44.1 k and at 192 k.
	if (static_cast<double> (frames) / static_cast<double> (rate) > kMaxSampleSeconds)
		return SampleStatus::TooLong;

	out.frameCount = static_cast<int> (frames);
	out.sourceRate = static_cast<double> (rate);
	out.samples.assign (frames * kSampleChannels, 0.f);

	float peak = 0.f;
	for (std::size_t frame = 0; frame < frames; ++frame)
	{
		const unsigned char* source = audio + frame * frameBytes;

		// MONO IS DUPLICATED, not panned left.
		const float left  = decodeSample (source, format, bits);
		const float right = (channels >= 2)
		                        ? decodeSample (source + bytesPerSample, format, bits)
		                        : left;

		out.samples[frame * kSampleChannels]     = left;
		out.samples[frame * kSampleChannels + 1] = right;

		peak = std::max (peak, std::max (std::fabs (left), std::fabs (right)));
	}

	out.peak = peak;
	return SampleStatus::Loaded;
}

//------------------------------------------------------------------------
SampleStatus loadWavFile (const std::string& path, SampleBuffer& out)
{
	out = SampleBuffer ();

	if (path.empty ())
		return SampleStatus::Empty;

	std::ifstream file (path, std::ios::binary);
	if (!file)
		return SampleStatus::Unreadable;

	file.seekg (0, std::ios::end);
	const std::streamoff length = file.tellg ();
	file.seekg (0, std::ios::beg);

	if (length <= 0)
		return SampleStatus::Unreadable;

	// A ceiling on what is read at all, before any header is trusted, so
	// a pathological file cannot ask for an arbitrary allocation. The
	// real limit is kMaxSampleSeconds; 20 s of 8-channel 64-bit float at
	// 192 k is under 250 MB.
	constexpr std::streamoff kMaxFileBytes = 256ll * 1024ll * 1024ll;
	if (length > kMaxFileBytes)
		return SampleStatus::TooLong;

	std::vector<unsigned char> bytes (static_cast<std::size_t> (length));
	if (!file.read (reinterpret_cast<char*> (bytes.data ()), length))
		return SampleStatus::Unreadable;

	return parseWav (bytes.data (), bytes.size (), out);
}

//------------------------------------------------------------------------
bool isWavPath (const std::string& path)
{
	// ".wav" alone is a hidden file with no name, not a sample.
	const std::string name = fileNameOf (path);
	return !name.empty () && name.front () != '.' && endsWithNoCase (name, ".wav");
}

//------------------------------------------------------------------------
std::string fileNameOf (const std::string& path)
{
	if (!path.empty () && (path.back () == '/' || path.back () == '\\'))
		return std::string ();

	const std::size_t slash = path.find_last_of ("/\\");
	return (slash == std::string::npos) ? path : path.substr (slash + 1);
}

//------------------------------------------------------------------------
std::string fileStemOf (const std::string& path)
{
	const std::string name = fileNameOf (path);
	const std::size_t dot = name.find_last_of ('.');
	if (dot == std::string::npos || dot == 0)
		return name;
	return name.substr (0, dot);
}

//------------------------------------------------------------------------
std::string wavPathFromDragText (const std::string& text)
{
	std::string line = firstLine (text);
	if (line.empty ())
		return std::string ();

	// "file://", an authority that is empty for a local file, then the
	// path - so the third slash is where the path starts.
	const std::string scheme = "file://";
	if (line.compare (0, scheme.size (), scheme) == 0)
	{
		const std::size_t slash = line.find ('/', scheme.size ());
		if (slash == std::string::npos)
			return std::string ();

		line = percentDecode (line.substr (slash));
	}

	return isWavPath (line) ? line : std::string ();
}

//------------------------------------------------------------------------
} // namespace SampleDrum
