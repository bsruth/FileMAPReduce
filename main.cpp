#define NOMINMAX
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

namespace fs = std::experimental::filesystem;

struct Transition {
	std::string file;
	bool on;
	uint64_t timestamp;
	int frameNumber;
};

const uint64_t InvalidTS = 0xABABAB;
struct Sample {
	uint64_t timestamp;
	int16_t value;
};


std::vector<Sample> GetSamplesFromDAT(const fs::path& filePath, int channelIndex, int numChannels, double timePerSample)
{
	if (fs::exists(filePath) == false) {
		return std::vector<Sample>();
	}

	auto peakPred = [](const auto& prev, const auto& next) {
		return (prev.value > 0 && prev.value > next.value)
			|| (prev.value < 0 && prev.value < next.value);
	};

	auto findInflect = [](const auto& prev, const auto& next) {

		auto lowToHigh = (prev.value <= 0) && (next.value > 0);
		auto highToLow = (prev.value >= 0) && (next.value < 0);
		return  lowToHigh || highToLow;
	};

	std::vector<Sample> samples;
	FILE* layFile = nullptr;
	fopen_s(&layFile, filePath.string().c_str(), "rb");
	using SampleValueType = decltype(Sample::value);

	int recordChunkSize = 10000;
	size_t samplesPerChunk = numChannels * recordChunkSize;
	std::vector<SampleValueType> dataBuffer(samplesPerChunk + recordChunkSize, 0); //one extra for padding
	int sampleIndex = 0;
	uint64_t sampleTimestamp = 0;
	auto samplesRead = fread_s(dataBuffer.data(), dataBuffer.size() * sizeof(SampleValueType), sizeof(SampleValueType), samplesPerChunk, layFile);
	if (samplesRead <= 0) {
		return samples;
	}
	while (samplesRead > 0) {
		for (size_t sampleOffset = channelIndex; sampleOffset < samplesRead; sampleOffset += numChannels) {
			Sample sample;
			sample.timestamp = sampleTimestamp;
			sample.value = dataBuffer[sampleOffset];
			samples.emplace_back(sample);
			++sampleIndex;
			sampleTimestamp = static_cast<uint64_t>(timePerSample * sampleIndex);
		}
		samplesRead = fread_s(dataBuffer.data(), dataBuffer.size() * sizeof(SampleValueType), sizeof(SampleValueType), samplesPerChunk, layFile);
	}
	fclose(layFile);
	return samples;
}


std::vector<Transition> GetTransitionsFromDAT(const fs::path& filePath, int channelIndex, int numChannels, double timePerSample)
{
	if (fs::exists(filePath) == false) {
		return std::vector<Transition>();
	}

	auto peakPred = [](const auto& prev, const auto& next) {
		return (prev.value > 0 && prev.value > next.value)
			|| (prev.value < 0 && prev.value < next.value);
	};

	auto findInflect = [](const auto& prev, const auto& next) {

		auto lowToHigh = (prev.value <= 0) && (next.value > 0);
		auto highToLow = (prev.value >= 0) && (next.value < 0);
		return  lowToHigh || highToLow;
	};

	std::vector<Transition> transitions;
	std::vector<Sample> samples;
	FILE* layFile = nullptr;
	fopen_s(&layFile, filePath.string().c_str(), "rb");
	using SampleValueType = decltype(Sample::value);

	int recordChunkSize = 10000;
	size_t samplesPerChunk = numChannels * recordChunkSize;
	std::vector<SampleValueType> dataBuffer(samplesPerChunk + recordChunkSize, 0); //one extra for padding
	int sampleIndex = 0;
	uint64_t sampleTimestamp = 0;
	auto samplesRead = fread_s(dataBuffer.data(), dataBuffer.size() * sizeof(SampleValueType), sizeof(SampleValueType), samplesPerChunk, layFile);
	if (samplesRead <= 0) {
		return transitions;
	}

	Sample prev;
	prev.timestamp = sampleTimestamp;
	prev.value = dataBuffer[channelIndex];
	bool foundInflection = false;
	bool foundPeak = false;
	while (samplesRead > 0) {
		for (size_t sampleOffset = channelIndex; sampleOffset < samplesRead; sampleOffset += numChannels) {
			Sample sample;
			sample.timestamp = sampleTimestamp;
			sample.value = dataBuffer[sampleOffset];

			if (foundInflection == false) {
				foundInflection = findInflect(prev, sample);
			}
			else if (foundPeak == false) {
				foundPeak = peakPred(prev, sample);
			}

			if (foundInflection && foundPeak) {
				Transition transition;
				bool on = prev.value > 0;
				transition.file = "DAT";
				transition.on = on;
				transition.timestamp = prev.timestamp;
				transition.frameNumber = sampleIndex;
				transitions.emplace_back(transition);
				foundInflection = false;
				foundPeak = false;
			}

			prev = sample;
			++sampleIndex;
			sampleTimestamp = static_cast<uint64_t>(timePerSample * sampleIndex);
		}
		samplesRead = fread_s(dataBuffer.data(), dataBuffer.size() * sizeof(SampleValueType), sizeof(SampleValueType), samplesPerChunk, layFile);
	}
	fclose(layFile);
	return transitions;
}


class SampleGenerator {
public:
	SampleGenerator(FILE* file, int channelIndex, int numChannels, double timePerSample)
		: file_(file)
		, channelIndex_(channelIndex)
		, numChannels_(numChannels)
		, timePerSample_(timePerSample)
	{
		recordChunkSize = 10000;
		samplesPerChunk = numChannels * recordChunkSize;
		dataBuffer = std::vector<SampleValueType>(samplesPerChunk + recordChunkSize, 0); //one extra for padding
		sampleIndex = 0;
		sampleTimestamp = -timePerSample;
		lastReadSampleIndex = 0;
		maxCurrentSampleOffset = 0;

	}
	~SampleGenerator() {}

	Sample GetNextSample()
	{
		++sampleIndex;
		sampleTimestamp += timePerSample_;

		if ( sampleIndex >= lastReadSampleIndex) {
			auto samplesRead = fread_s(dataBuffer.data(), dataBuffer.size() * sizeof(SampleValueType), sizeof(SampleValueType), samplesPerChunk, file_);
			if (samplesRead <= 0) {
				Sample s;
				s.timestamp = InvalidTS;
				return s;
			}
			lastReadSampleIndex = (samplesRead / numChannels_);
			sampleIndex = 0;
		}
		
		Sample s;
		auto offset = sampleIndex * numChannels_;
		s.value = dataBuffer[offset];
		s.timestamp = static_cast<uint64_t>(sampleTimestamp);
		return s;
	}

private:
	FILE* file_;
	int channelIndex_;
	int numChannels_;
	double timePerSample_;

	using SampleValueType = decltype(Sample::value);
	int recordChunkSize = 10000;
	size_t samplesPerChunk;
	std::vector<SampleValueType> dataBuffer;
	int sampleIndex;
	double sampleTimestamp;
	int lastReadSampleIndex;
	int maxCurrentSampleOffset;
	int samplesProcessed;
};

class TransitionFinder {
public:
	TransitionFinder(SampleGenerator sGen_)
		: foundInflection(false)
		, foundPeak(false)
		, sGen(sGen_)
	{
		prev = sGen.GetNextSample();
	}
	~TransitionFinder() {}

	Transition GetNextTransition() {
		Transition nextTransition;
		nextTransition.timestamp = InvalidTS;
		auto current = sGen.GetNextSample();
		while (current.timestamp != InvalidTS && nextTransition.timestamp == InvalidTS) {
			if (foundInflection == false) {
				foundInflection = IsInflection(prev, current);
			}
			else if (foundPeak == false) {
				foundPeak = IsPeak(prev, current);
			}

			if (foundInflection && foundPeak) {
				bool on = prev.value > 0;
				nextTransition.file = "DAT";
				nextTransition.on = on;
				nextTransition.timestamp = prev.timestamp;
				nextTransition.frameNumber = 0;
				foundInflection = false;
				foundPeak = false;
				prev = current;

			}
			else {
				prev = std::exchange(current, sGen.GetNextSample());
			}
		}

		return nextTransition;
	}

private:

	bool IsPeak(const Sample& prev, const Sample& next) {
		return (prev.value > 0 && prev.value > next.value)
			|| (prev.value < 0 && prev.value < next.value);
	};

	bool IsInflection (const Sample& prev, const Sample& next) {
		auto lowToHigh = (prev.value <= 0) && (next.value > 0);
		auto highToLow = (prev.value >= 0) && (next.value < 0);
		return  lowToHigh || highToLow;
	};
	bool foundInflection;
	bool foundPeak;
	SampleGenerator sGen;
	Sample prev;
};
std::vector<Sample> GetSamplesFromDAT2(const fs::path& filePath, int channelIndex, int numChannels, double timePerSample) {
	if (fs::exists(filePath) == false) {
		return std::vector<Sample>();
	}

	std::vector<Sample> samples;

	FILE* layFile = nullptr;
	fopen_s(&layFile, filePath.string().c_str(), "rb");

	SampleGenerator sGen(layFile, channelIndex, numChannels, timePerSample);

	auto s = sGen.GetNextSample();
	while (s.timestamp != InvalidTS) {
		samples.push_back(s);
		s = sGen.GetNextSample();
	}
	return samples;
	fclose(layFile);

}
std::vector<Transition> GetTransitionsFromDAT2(const fs::path& filePath, int channelIndex, int numChannels, double timePerSample)
{
	if (fs::exists(filePath) == false) {
		return std::vector<Transition>();
	}

	std::vector<Transition> transitions;
	std::vector<Sample> samples;
	FILE* layFile = nullptr;
	fopen_s(&layFile, filePath.string().c_str(), "rb");
	
	SampleGenerator sGen(layFile, channelIndex, numChannels, timePerSample);

	TransitionFinder tF(sGen);

	auto transition = tF.GetNextTransition();
	while (transition.timestamp != InvalidTS) {
		transitions.emplace_back(transition);
		transition = tF.GetNextTransition();
	}

	fclose(layFile);
	return transitions;
}
using namespace std;
void main()
{
	const fs::path DATPath(R"(C:\Users\Brian\Desktop\VTSync\TestData\Set9\NlxCSG.dat)");
	auto samplingFreq = 32000;
	auto timePerSample(1.0 / samplingFreq * std::micro::den);
	auto channelsInLay = 64;
	auto startTime = std::chrono::steady_clock::now();
	auto datTransitions = GetTransitionsFromDAT(DATPath, 0, channelsInLay, timePerSample);
	auto execTime = std::chrono::steady_clock::now() - startTime;
	cout << " Old way took: " << std::chrono::duration_cast<std::chrono::microseconds>(execTime).count() << " us" << endl;

	startTime = std::chrono::steady_clock::now();
	auto datTransitions2 = GetTransitionsFromDAT2(DATPath, 0, channelsInLay, timePerSample);
	execTime = std::chrono::steady_clock::now() - startTime;
	cout << " New way took: " << std::chrono::duration_cast<std::chrono::microseconds>(execTime).count() << " us" << endl;


	auto mismatched = std::mismatch(begin(datTransitions), end(datTransitions), begin(datTransitions2), end(datTransitions2), [](const auto& lhs, const auto& rhs) {
		return lhs.timestamp == rhs.timestamp
			&& lhs.on == rhs.on;
	});

	auto areEqual = std::equal(begin(datTransitions), end(datTransitions), begin(datTransitions2), end(datTransitions2), [](const auto& lhs, const auto& rhs) {
		return lhs.timestamp == rhs.timestamp
			&& lhs.on == rhs.on;
	});

	std::cout << (mismatched.first == end(datTransitions) ? "Values equal" : "NOPE") << endl;
//#ifdef _DEBUG
	system("pause");
//#endif
}
