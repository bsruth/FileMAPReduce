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

struct Sample {
	uint64_t timestamp;
	int16_t value;
};


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


Sample MapValueToSample(int16_t* buffer, int channelToMap, uint64_t timestamp) 
{
	Sample s;
	s.timestamp = timestamp;
	s.value = buffer[channelToMap];
	return s;
}
std::vector<Transition> GetTransitionsFromDAT2(const fs::path& filePath, int channelIndex, int numChannels, double timePerSample)
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
		for (size_t sampleOffset = 0; sampleOffset < samplesRead; sampleOffset += numChannels) {
			auto sample = MapValueToSample(&dataBuffer[sampleOffset], channelIndex, sampleTimestamp);
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
using namespace std;
void main()
{
	const fs::path DATPath(R"(C:\Users\Brian\Desktop\VTSync\TestData\Set9\NlxCSG.dat)");
	auto samplingFreq = 32000;
	auto timePerSample(1.0 / samplingFreq * std::micro::den);
	auto channelsInLay = 64;
	auto datTransitions = GetTransitionsFromDAT(DATPath, 0, channelsInLay, timePerSample);

	auto datTransitions2 = GetTransitionsFromDAT2(DATPath, 0, channelsInLay, timePerSample);


	auto areEqual = std::equal(begin(datTransitions), end(datTransitions), begin(datTransitions2), end(datTransitions2), [](const auto& lhs, const auto& rhs) {
		return lhs.timestamp == rhs.timestamp
			&& lhs.on == rhs.on;
	});

	std::cout << (areEqual ? "Values equal" : "NOPE") << endl;
#ifdef _DEBUG
	system("pause");
#endif
}