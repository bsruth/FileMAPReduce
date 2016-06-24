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


Sample MapValueToSample(int16_t* buffer, int channelToMap, uint64_t timestamp) 
{
	Sample s;
	s.timestamp = timestamp;
	s.value = buffer[channelToMap];
	return s;
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
	
	SampleGenerator sGen(layFile, channelIndex, numChannels, timePerSample);

	Sample current = sGen.GetNextSample();
	Sample prev = current;
	bool foundInflection = false;
	bool foundPeak = false;
	while (current.timestamp != InvalidTS) {
			if (foundInflection == false) {
				foundInflection = findInflect(prev, current);
			}
			else if (foundPeak == false) {
				foundPeak = peakPred(prev, current);
			}

			if (foundInflection && foundPeak) {
				Transition transition;
				bool on = prev.value > 0;
				transition.file = "DAT";
				transition.on = on;
				transition.timestamp = prev.timestamp;
				transition.frameNumber = 0;
				transitions.emplace_back(transition);
				foundInflection = false;
				foundPeak = false;
			}

			prev = std::exchange(current, sGen.GetNextSample());
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


	auto mismatched = std::mismatch(begin(datTransitions), end(datTransitions), begin(datTransitions2), end(datTransitions2), [](const auto& lhs, const auto& rhs) {
		return lhs.timestamp == rhs.timestamp
			&& lhs.on == rhs.on;
	});

	auto areEqual = std::equal(begin(datTransitions), end(datTransitions), begin(datTransitions2), end(datTransitions2), [](const auto& lhs, const auto& rhs) {
		return lhs.timestamp == rhs.timestamp
			&& lhs.on == rhs.on;
	});

	std::cout << (mismatched.first == end(datTransitions) ? "Values equal" : "NOPE") << endl;
#ifdef _DEBUG
	system("pause");
#endif
}
