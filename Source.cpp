#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

// typedefs
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t int32;

const float c_pi = 3.14159265359f;

// https://stackoverflow.com/a/24207339
#ifndef errno_t
#define errno_t int
#endif

// define Microsoft non-portable functions
// https://stackoverflow.com/a/1513215
#ifndef fopen_s
errno_t fopen_s(FILE** f, const char* name, const char* mode) {
  errno_t ret = 0;
  assert(f);
  *f = fopen(name, mode);
  /* Can't be sure about 1-to-1 mapping of errno and MS' errno_t */
  if (!*f) ret = errno;
  return ret;
}
#endif

// this struct is the minimal required header data for a wav file
struct SMinimalWaveFileHeader {
  // the main chunk
  unsigned char m_chunkID[4];
  uint32 m_chunkSize;
  unsigned char m_format[4];

  // sub chunk 1 "fmt "
  unsigned char m_subChunk1ID[4];
  uint32 m_subChunk1Size;
  uint16 m_audioFormat;
  uint16 m_numChannels;
  uint32 m_sampleRate;
  uint32 m_byteRate;
  uint16 m_blockAlign;
  uint16 m_bitsPerSample;

  // sub chunk 2 "data"
  unsigned char m_subChunk2ID[4];
  uint32 m_subChunk2Size;

  // then comes the data!
};

enum class ECrossFade {
  None,
  In,
  Out,
};

inline void FloatToPCM(unsigned char* PCM, const float& in, size_t numBytes) {
  // 8 bit is unsigned
  if (numBytes == 1) {
    PCM[0] = static_cast<unsigned char>((in * 0.5f + 0.5f) * 255.0f);
    return;
  }

  // casting to double because floats can't exactly store 0x7fffffff, but
  // doubles can.
  // Details of that:
  // https://blog.demofox.org/2017/11/21/floating-point-precision/
  uint32 data;
  if (in < 0.0f) {
    data = static_cast<uint32>(static_cast<double>(in) *
                               static_cast<double>(0x80000000));
  } else {
    data = static_cast<uint32>(static_cast<double>(in) *
                               static_cast<double>(0x7fffffff));
  }

  switch (numBytes) {
    case 4:
      PCM[3] = ((data >> 24) & 0xFF);
      PCM[2] = ((data >> 16) & 0xFF);
      PCM[1] = ((data >> 8) & 0xFF);
      PCM[0] = (data & 0xFF);
      break;
    case 3:
      PCM[2] = ((data >> 24) & 0xFF);
      PCM[1] = ((data >> 16) & 0xFF);
      PCM[0] = ((data >> 8) & 0xFF);
      break;
    case 2:
      PCM[1] = ((data >> 24) & 0xFF);
      PCM[0] = ((data >> 16) & 0xFF);
      break;
  }
}

inline void PCMToFloat(float* out, const unsigned char* PCM, size_t numBytes) {
  // 8 bit is unsigned
  if (numBytes == 1) {
    *out =
        (static_cast<float>(PCM[0]) / static_cast<float>(255.0f)) * 2.0f - 1.0f;
    return;
  }

  uint32 data = 0;
  switch (numBytes) {
    case 4:
      data = (static_cast<uint32>(PCM[3]) << 24) |
             (static_cast<uint32>(PCM[2]) << 16) |
             (static_cast<uint32>(PCM[1]) << 8) | static_cast<uint32>(PCM[0]);
      break;
    case 3:
      data = (static_cast<uint32>(PCM[2]) << 24) |
             (static_cast<uint32>(PCM[1]) << 16) |
             (static_cast<uint32>(PCM[0]) << 8);
      break;
    case 2:
      data = (static_cast<uint32>(PCM[1]) << 24) |
             (static_cast<uint32>(PCM[0]) << 16);
      break;
  }

  // casting to double because floats can't exactly store 0x7fffffff, but
  // doubles can.
  // Details of that:
  // https://blog.demofox.org/2017/11/21/floating-point-precision/
  if (data & 0x80000000) {
    *out = static_cast<float>(static_cast<double>(static_cast<int32>(data)) /
                              static_cast<double>(0x80000000));
  } else {
    *out = static_cast<float>(static_cast<double>(data) /
                              static_cast<double>(0x7fffffff));
  }
}

// numBytes can be 1, 2, 3, or 4.
// Coresponding to 8 bit, 16 bit, 24 bit, and 32 bit audio.
bool WriteWaveFile(const char* fileName, std::vector<float>* dataFloat,
                   uint16 numChannels, uint32 sampleRate, uint16 numBytes) {
  std::vector<unsigned char> data;
  data.resize(dataFloat->size() * numBytes);
  for (size_t i = 0; i < dataFloat->size(); ++i)
    FloatToPCM((unsigned char*)&data[i * numBytes], (*dataFloat)[i], numBytes);

  uint32 dataSize = static_cast<uint32>(data.size());
  uint16 bitsPerSample = numBytes * 8;

  // open the file if we can
  FILE* File = nullptr;
  fopen_s(&File, fileName, "w+b");
  if (!File) {
    printf("[-----ERROR-----] Could not open %s for writing.\n", fileName);
    return false;
  }

  SMinimalWaveFileHeader waveHeader;

  // fill out the main chunk
  memcpy(waveHeader.m_chunkID, "RIFF", 4);
  waveHeader.m_chunkSize = dataSize + 36;
  memcpy(waveHeader.m_format, "WAVE", 4);

  // fill out sub chunk 1 "fmt "
  memcpy(waveHeader.m_subChunk1ID, "fmt ", 4);
  waveHeader.m_subChunk1Size = 16;
  waveHeader.m_audioFormat = 1;
  waveHeader.m_numChannels = numChannels;
  waveHeader.m_sampleRate = sampleRate;
  waveHeader.m_byteRate = sampleRate * numChannels * bitsPerSample / 8;
  waveHeader.m_blockAlign = numChannels * bitsPerSample / 8;
  waveHeader.m_bitsPerSample = bitsPerSample;

  // fill out sub chunk 2 "data"
  memcpy(waveHeader.m_subChunk2ID, "data", 4);
  waveHeader.m_subChunk2Size = dataSize;

  // write the header
  fwrite(&waveHeader, sizeof(SMinimalWaveFileHeader), 1, File);

  // write the wave data itself
  fwrite(&data[0], dataSize, 1, File);

  // close the file and return success
  fclose(File);
  printf("%s saved.\n", fileName);
  return true;
}

bool ReadFileIntoMemory(const char* fileName,
                        std::vector<unsigned char>* data) {
  // open the file if we can
  FILE* file = nullptr;
  fopen_s(&file, fileName, "rb");
  if (!file) {
    printf("[-----ERROR-----]Could not open %s for reading.\n", fileName);
    return false;
  }

  // get the file size and resize the vector to hold the data
  fseek(file, 0, SEEK_END);
  data->resize(ftell(file));

  // read the file into the vector
  fseek(file, 0, SEEK_SET);
  fread(&(*data)[0], 1, data->size(), file);

  // return success
  fclose(file);
  return true;
}

bool ReadWaveFile(const char* fileName, std::vector<float>* data,
                  uint16* numChannels, uint32* sampleRate, uint16* numBytes) {
  // read the whole file into memory if we can
  std::vector<unsigned char> fileData;
  if (!ReadFileIntoMemory(fileName, &fileData)) return false;
  size_t fileIndex = 0;

  // make sure the main chunk ID is "RIFF"
  if ((fileData.size() < fileIndex + 4) ||
      memcmp(&fileData[fileIndex], "RIFF", 4)) {
    printf("[-----ERROR-----]%s is an invalid input file. (1)\n", fileName);
    return false;
  }
  fileIndex += 4;

  // get the main chunk size
  uint32 chunkSize;
  if (fileData.size() < fileIndex + 4) {
    printf("[-----ERROR-----]%s is an invalid input file. (2)\n", fileName);
    return false;
  }
  chunkSize = *(uint32*)&fileData[fileIndex];
  fileIndex += 4;

  // make sure the format is "WAVE"
  if ((fileData.size() < fileIndex + 4) ||
      memcmp(&fileData[fileIndex], "WAVE", 4)) {
    printf("[-----ERROR-----]%s is an invalid input file. (3)\n", fileName);
    return false;
  }
  fileIndex += 4;

  size_t chunkPosFmt = -1;
  size_t chunkPosData = -1;
  while (chunkPosFmt == -1 || chunkPosData == -1) {
    // get a chunk id and chunk size if we can
    if (fileData.size() < fileIndex + 8) {
      printf("[-----ERROR-----]%s is an invalid input file. (4)\n", fileName);
      return false;
    }

    // get the chunk id if we can
    const unsigned char* chunkID = (unsigned char*)&fileData[fileIndex];
    fileIndex += 4;
    chunkSize = *(uint32*)&fileData[fileIndex];
    fileIndex += 4;

    // if we hit a fmt
    if (!memcmp(chunkID, "fmt ", 4)) {
      chunkPosFmt = static_cast<size_t>(fileIndex - 8);
      // else if we hit a data
    } else if (!memcmp(chunkID, "data", 4)) {
      chunkPosData = static_cast<size_t>(fileIndex - 8);
    }

    // skip to the next chunk
    fileIndex += chunkSize;
  }

  // we'll use this handy struct to load in
  SMinimalWaveFileHeader waveData;

  // load the fmt part if we can
  fileIndex = chunkPosFmt;
  if (fileData.size() < fileIndex + 24) {
    printf("[-----ERROR-----]%s is an invalid input file. (5)\n", fileName);
    return false;
  }
  memcpy(&waveData.m_subChunk1ID, &fileData[fileIndex], 24);
  fileIndex += 24;

  // load the data part if we can
  fileIndex = chunkPosData;
  if (fileData.size() < fileIndex + 8) {
    printf("[-----ERROR-----]%s is an invalid input file. (6)\n", fileName);
    return false;
  }
  memcpy(&waveData.m_subChunk2ID, &fileData[fileIndex], 8);
  fileIndex += 8;

  // verify a couple things about the file data
  if (waveData.m_audioFormat != 1 ||        // only pcm data
      waveData.m_numChannels < 1 ||         // must have a channel
      waveData.m_numChannels > 2 ||         // must not have more than 2
      waveData.m_bitsPerSample > 32 ||      // 32 bits per sample max
      waveData.m_bitsPerSample % 8 != 0 ||  // must be a multiple of 8 bites
      waveData.m_blockAlign > 8) {          // blocks must be 8 bytes or lower
    printf("[-----ERROR-----]%s is an invalid input file. (7)\n", fileName);
    return false;
  }

  // figure out how many samples and blocks there are total in the source data
  size_t bytesPerSample = waveData.m_blockAlign / waveData.m_numChannels;
  size_t numSourceSamples = waveData.m_subChunk2Size / bytesPerSample;

  // allocate space for the source samples
  data->resize(numSourceSamples);

  // read in the source samples at whatever sample rate / number of channels it
  // might be in
  if (fileData.size() < fileIndex + numSourceSamples * bytesPerSample) {
    printf("[-----ERROR-----]%s is an invalid input file. (8)\n", fileName);
    return false;
  }

  for (size_t nIndex = 0; nIndex < numSourceSamples; ++nIndex) {
    PCMToFloat(&((*data)[nIndex]), &fileData[fileIndex], bytesPerSample);
    fileIndex += bytesPerSample;
  }

  // return our data
  *numChannels = waveData.m_numChannels;
  *sampleRate = waveData.m_sampleRate;
  *numBytes = waveData.m_bitsPerSample / 8;

  printf("%s loaded.\n", fileName);
  return true;
}

// Cubic hermite interpolation. More information available here:
// https://blog.demofox.org/2015/08/08/cubic-hermite-interpolation/
// t is a value that goes from 0 to 1 to interpolate in a C1 continuous way
// across uniformly sampled data points.
// when t is 0, this will return B. When t is 1, this will return C.
static float CubicHermite(float A, float B, float C, float D, float t) {
  float a = -A / 2.0f + (3.0f * B) / 2.0f - (3.0f * C) / 2.0f + D / 2.0f;
  float b = A - (5.0f * B) / 2.0f + 2.0f * C - D / 2.0f;
  float c = -A / 2.0f + C / 2.0f;
  float d = B;

  return a * t * t * t + b * t * t + c * t + d;
}

inline float SampleChannelFractional(const std::vector<float>& input,
                                     float sampleFloat, uint16 channel,
                                     uint16 numChannels) {
// change this to #if 0 to use linear interpolation instead, which is faster but
// lower quality
#if 1

  // This uses cubic hermite interpolation to get values between samples

  size_t sample = size_t(sampleFloat);
  float sampleFraction = sampleFloat - std::floor(sampleFloat);

  size_t sampleIndexNeg1 = (sample > 0) ? sample - 1 : sample;
  size_t sampleIndex0 = sample;
  size_t sampleIndex1 = sample + 1;
  size_t sampleIndex2 = sample + 2;

  sampleIndexNeg1 = sampleIndexNeg1 * numChannels + channel;
  sampleIndex0 = sampleIndex0 * numChannels + channel;
  sampleIndex1 = sampleIndex1 * numChannels + channel;
  sampleIndex2 = sampleIndex2 * numChannels + channel;

  sampleIndexNeg1 = std::min(sampleIndexNeg1, input.size() - 1);
  sampleIndex0 = std::min(sampleIndex0, input.size() - 1);
  sampleIndex1 = std::min(sampleIndex1, input.size() - 1);
  sampleIndex2 = std::min(sampleIndex2, input.size() - 1);

  return CubicHermite(input[sampleIndexNeg1], input[sampleIndex0],
                      input[sampleIndex1], input[sampleIndex2], sampleFraction);
#else

  // This uses linear interpolation to get values between samples.

  size_t sample = size_t(sampleFloat);
  float sampleFraction = sampleFloat - std::floor(sampleFloat);

  size_t sample1Index = sample * numChannels + channel;
  sample1Index = std::min(sample1Index, input.size() - 1);
  float value1 = input[sample1Index];

  size_t sample2Index = (sample + 1) * numChannels + channel;
  sample2Index = std::min(sample2Index, input.size() - 1);
  float value2 = input[sample1Index];

  return value1 * (1.0f - sampleFraction) + value2 * sampleFraction;
#endif
}

// Resample
void TimeAdjust(const std::vector<float>& input, std::vector<float>* output,
                uint16 numChannels, float timeMultiplier) {
  size_t numSrcSamples = input.size() / numChannels;
  size_t numOutSamples =
      (size_t)(static_cast<float>(numSrcSamples) * timeMultiplier);
  output->resize(numOutSamples * numChannels);

  for (size_t outSample = 0; outSample < numOutSamples; ++outSample) {
    float percent =
        static_cast<float>(outSample) / static_cast<float>(numOutSamples - 1);

    float srcSampleFloat = static_cast<float>(numSrcSamples) * percent;

    for (uint16 channel = 0; channel < numChannels; ++channel)
      (*output)[outSample * numChannels + channel] =
          SampleChannelFractional(input, srcSampleFloat, channel, numChannels);
  }
}

// writes a grain to the output buffer, applying a fade in or fade out at the
// beginning if it should, as well as a pitch multiplier (playback speed
// multiplier) for the grain
size_t SplatGrainToOutput(const std::vector<float>& input,
                          std::vector<float>* output, uint16 numChannels,
                          size_t grainStart, size_t grainSize,
                          size_t outputSampleIndex, ECrossFade crossFade,
                          size_t crossFadeSize, float pitchMultiplier,
                          bool isFinalGrain) {
  // calculate starting indices
  size_t outputIndex = outputSampleIndex * numChannels;

  // write the samples
  size_t numSamplesWritten = 0;
  for (float sample = 0; sample < static_cast<float>(grainSize);
       sample += pitchMultiplier) {
    // break out of the loop if we are out of bounds on the input or output
    if (outputIndex + numChannels > output->size()) break;

    float inputIndexSamples = static_cast<float>(grainStart) + sample;
    if (size_t(inputIndexSamples) * numChannels + numChannels > input.size())
      break;

    // calculate envelope for this sample
    float envelope = 1.0f;
    if (crossFade != ECrossFade::None) {
      if (sample <= static_cast<float>(crossFadeSize))
        envelope = sample / static_cast<float>(crossFadeSize);
      if (crossFade == ECrossFade::Out) envelope = 1.0f - envelope;
    }

    // write the enveloped sample
    for (uint16 channel = 0; channel < numChannels; ++channel)
      (*output)[outputIndex + channel] +=
          SampleChannelFractional(input, inputIndexSamples, channel,
                                  numChannels) *
          envelope;

    // move to the next samples
    outputIndex += numChannels;
    ++numSamplesWritten;
  }

  // report an error if ever the cross fade size was bigger than the actual
  // grain size, since this causes popping and would be hard to find the cause
  // of.
  // suppress error on final grain since there can be false positives due to
  // sound ending. That makes false negatives but calling this good enough.
  if (!isFinalGrain && crossFadeSize > numSamplesWritten) {
    static bool reportedError = false;
    if (!reportedError) {
      printf(
          "[-----ERROR-----] cross fade is longer than a grain size! (error "
          "only reported once)\n");
      reportedError = true;
    }
  }

  // return how many samples we wrote
  return numSamplesWritten;
}

void GranularTimePitchAdjust(const std::vector<float>& input,
                             std::vector<float>* output, uint16 numChannels,
                             uint32 sampleRate, float timeMultiplier,
                             float pitchMultiplier, float grainSizeSeconds,
                             float crossFadeSeconds) {
  // calculate size of output buffer and resize it
  size_t numInputSamples = input.size() / numChannels;
  size_t numOutputSamples =
      (size_t)(static_cast<float>(numInputSamples) * timeMultiplier);
  output->clear();
  output->resize(numOutputSamples * numChannels, 0.0f);

  // calculate how many grains are in the input data
  size_t grainSizeSamples =
      size_t(static_cast<float>(sampleRate) * grainSizeSeconds);
  size_t numGrains = numInputSamples / grainSizeSamples;
  if (numInputSamples % grainSizeSamples) numGrains++;

  // calculate the cross fade size
  size_t crossFadeSizeSamples =
      size_t(static_cast<float>(sampleRate) * crossFadeSeconds);

  // Repeat each grain 0 or more times to make the output be the correct size
  size_t outputSampleIndex = 0;
  size_t lastGrainWritten = -1;
  for (size_t grain = 0; grain < numGrains; ++grain) {
    // calculate the boundaries of the grain
    size_t inputGrainStart = grain * grainSizeSamples;

    // calculate the end of where this grain should go in the output buffer
    size_t outputSampleWindowEnd = static_cast<size_t>(
        static_cast<float>(inputGrainStart + grainSizeSamples) *
        timeMultiplier);

    // Splat out zero or more copies of the grain to get our output to be at
    // least as far as we want it to be.
    // Zero copies happens when we shorten time and need to cut pieces (grains)
    // out of the original sound
    while (outputSampleIndex < outputSampleWindowEnd) {
      bool isFinalGrain = (grain == numGrains - 1);

      // if we are writing our first grain, or the last grain we wrote was the
      // previous grain, then we don't need to do a cross fade`
      if ((lastGrainWritten == -1) || (lastGrainWritten == grain - 1)) {
        outputSampleIndex += SplatGrainToOutput(
            input, output, numChannels, inputGrainStart, grainSizeSamples,
            outputSampleIndex, ECrossFade::None, crossFadeSizeSamples,
            pitchMultiplier, isFinalGrain);
        lastGrainWritten = grain;
        continue;
      }

      // else we need to fade out the old grain and then fade in the new one.
      // NOTE: fading out the old grain means starting to play the grain after
      // the last one and bringing it's volume down to zero.
      SplatGrainToOutput(input, output, numChannels,
                         (lastGrainWritten + 1) * grainSizeSamples,
                         grainSizeSamples, outputSampleIndex, ECrossFade::Out,
                         crossFadeSizeSamples, pitchMultiplier, isFinalGrain);
      outputSampleIndex += SplatGrainToOutput(
          input, output, numChannels, inputGrainStart, grainSizeSamples,
          outputSampleIndex, ECrossFade::In, crossFadeSizeSamples,
          pitchMultiplier, isFinalGrain);
      lastGrainWritten = grain;
    }
  }
}

template <typename LAMBDA>
void GranularTimePitchAdjustDynamic(const std::vector<float>& input,
                                    std::vector<float>* output,
                                    uint16 numChannels, uint32 sampleRate,
                                    float grainSizeSeconds,
                                    float crossFadeSeconds,
                                    const LAMBDA& settingsCallback) {
  // calculate how many grains are in the input data
  size_t numInputSamples = input.size() / numChannels;
  size_t grainSizeSamples =
      static_cast<size_t>(static_cast<float>(sampleRate) * grainSizeSeconds);
  size_t numGrains = numInputSamples / grainSizeSamples;
  if (numInputSamples % grainSizeSamples) numGrains++;

  // calculate size of output buffer and resize it
  size_t numOutputSamples = 0;
  for (size_t i = 0; i < numGrains; ++i) {
    size_t grainStart = i * grainSizeSamples;
    size_t grainEnd = grainStart + grainSizeSamples;
    grainEnd = std::min(grainEnd, input.size());
    size_t grainSize = grainEnd - grainStart;

    float percent = static_cast<float>(i) / static_cast<float>(numGrains);
    float timeMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    settingsCallback(percent, timeMultiplier, pitchMultiplier);

    numOutputSamples +=
        static_cast<size_t>(static_cast<float>(grainSize) * timeMultiplier);
  }
  output->clear();
  output->resize(numOutputSamples * numChannels, 0.0f);

  // calculate the cross fade size
  size_t crossFadeSizeSamples =
      static_cast<size_t>(static_cast<float>(sampleRate) * crossFadeSeconds);

  // Repeat each grain 0 or more times to make the output be the correct size
  size_t outputSampleIndex = 0;
  size_t lastGrainWritten = -1;
  float lastGrainPitchMultiplier = 1.0f;
  size_t outputSampleWindowEnd = 0;
  for (size_t grain = 0; grain < numGrains; ++grain) {
    // calculate the boundaries of the grain
    size_t inputGrainStart = grain * grainSizeSamples;

    // calculate the end of where this grain should go in the output buffer
    float percent = static_cast<float>(grain) / static_cast<float>(numGrains);
    float timeMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    settingsCallback(percent, timeMultiplier, pitchMultiplier);
    outputSampleWindowEnd += static_cast<size_t>(
        static_cast<float>(grainSizeSamples) * timeMultiplier);

    // Splat out zero or more copies of the grain to get our output to be at
    // least as far as we want it to be.
    // Zero copies happens when we shorten time and need to cut pieces (grains)
    // out of the original sound
    while (outputSampleIndex < outputSampleWindowEnd) {
      bool isFinalGrain = (grain == numGrains - 1);

      // if we are writing our first grain, or the last grain we wrote was the
      // previous grain, then we don't need to do a cross fade`
      if ((lastGrainWritten == -1) || (lastGrainWritten == grain - 1)) {
        outputSampleIndex += SplatGrainToOutput(
            input, output, numChannels, inputGrainStart, grainSizeSamples,
            outputSampleIndex, ECrossFade::None, crossFadeSizeSamples,
            pitchMultiplier, isFinalGrain);
        lastGrainWritten = grain;
        lastGrainPitchMultiplier = pitchMultiplier;
        continue;
      }

      // else we need to fade out the old grain and then fade in the new one.
      // NOTE: fading out the old grain means starting to play the grain after
      // the last one and bringing it's volume down to zero, using the previous
      // grain's pitch multiplier.
      SplatGrainToOutput(
          input, output, numChannels, (lastGrainWritten + 1) * grainSizeSamples,
          grainSizeSamples, outputSampleIndex, ECrossFade::Out,
          crossFadeSizeSamples, lastGrainPitchMultiplier, isFinalGrain);
      outputSampleIndex += SplatGrainToOutput(
          input, output, numChannels, inputGrainStart, grainSizeSamples,
          outputSampleIndex, ECrossFade::In, crossFadeSizeSamples,
          pitchMultiplier, isFinalGrain);
      lastGrainWritten = grain;
      lastGrainPitchMultiplier = pitchMultiplier;
    }
  }
}

// the entry point of our application
int main(int argc, char** argv) {
  // load the wave file
  uint16 numChannels;
  uint32 sampleRate;
  uint16 numBytes;
  std::vector<float> source, out, sourceLeft, sourceRight;
  ReadWaveFile("data/legend1.wav", &source, &numChannels, &sampleRate,
               &numBytes);

  // speed up the audio and increase pitch
  {
    TimeAdjust(source, &out, numChannels, 0.7f);
    WriteWaveFile("data/out_A_FastHigh.wav", &out, numChannels, sampleRate,
                  numBytes);

    TimeAdjust(source, &out, numChannels, 0.4f);
    WriteWaveFile("data/out_A_FasterHigher.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // slow down the audio and decrease pitch
  {
    TimeAdjust(source, &out, numChannels, 1.3f);
    WriteWaveFile("data/out_A_SlowLow.wav", &out, numChannels, sampleRate,
                  numBytes);

    TimeAdjust(source, &out, numChannels, 2.1f);
    WriteWaveFile("data/out_A_SlowerLower.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // speed up audio without affecting pitch
  {
    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 0.7f, 1.0f,
                            0.02f, 0.002f);
    WriteWaveFile("data/out_B_Fast.wav", &out, numChannels, sampleRate,
                  numBytes);

    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 0.4f, 1.0f,
                            0.02f, 0.002f);
    WriteWaveFile("data/out_B_Faster.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // slow down audio without affecting pitch
  {
    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.3f, 1.0f,
                            0.02f, 0.002f);
    WriteWaveFile("data/out_B_Slow.wav", &out, numChannels, sampleRate,
                  numBytes);

    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 2.1f, 1.0f,
                            0.02f, 0.002f);
    WriteWaveFile("data/out_B_Slower.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // Make pitch higher without affecting length
  {
    // do it in two steps - first as a granular time adjust, and then as a
    // pitch/time adjust
    std::vector<float> out2;
    GranularTimePitchAdjust(source, &out2, numChannels, sampleRate, 1.0f / 0.7f,
                            1.0f, 0.02f, 0.002f);
    TimeAdjust(out2, &out, numChannels, 0.7f);
    WriteWaveFile("data/out_C_HighAlternate.wav", &out, numChannels, sampleRate,
                  numBytes);

    // do it in one step by changing grain playback speeds
    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.0f,
                            1.0f / 0.7f, 0.02f, 0.002f);
    WriteWaveFile("data/out_C_High.wav", &out, numChannels, sampleRate,
                  numBytes);

    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.0f,
                            1.0f / 0.4f, 0.02f, 0.002f);
    WriteWaveFile("data/out_C_Higher.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // make pitch lower without affecting length
  {
    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.0f,
                            1.0f / 1.3f, 0.02f, 0.002f);
    WriteWaveFile("data/out_C_Low.wav", &out, numChannels, sampleRate,
                  numBytes);

    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.0f,
                            1.0f / 2.1f, 0.02f, 0.002f);
    WriteWaveFile("data/out_C_Lower.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // Make pitch lower but speed higher
  {
    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 1.3f,
                            1.0f / 0.7f, 0.02f, 0.002f);
    WriteWaveFile("data/out_D_SlowHigh.wav", &out, numChannels, sampleRate,
                  numBytes);

    GranularTimePitchAdjust(source, &out, numChannels, sampleRate, 0.7f,
                            1.0f / 1.3f, 0.02f, 0.002f);
    WriteWaveFile("data/out_D_FastLow.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  // dynamic tests which change time and pitch multipliers over time (for each
  // input grain)
  {
    // adjust pitch on a sine wave
    GranularTimePitchAdjustDynamic(
        source, &out, numChannels, sampleRate, 0.02f, 0.002f,
        [](float percent, float& timeMultiplier, float& pitchMultiplier) {
          // time is 1
          // pitch is 10hz from 0.75 to 1.25
          timeMultiplier = 1.0f;
          pitchMultiplier =
              1.0f /
              ((std::sin(percent * c_pi * 10.0f) * 0.5f + 0.5f) * 0.5f + 0.75f);
        });
    WriteWaveFile("data/out_E_Pitch.wav", &out, numChannels, sampleRate,
                  numBytes);

    // adjust speed on a sine wave
    GranularTimePitchAdjustDynamic(
        source, &out, numChannels, sampleRate, 0.02f, 0.002f,
        [](float percent, float& timeMultiplier, float& pitchMultiplier) {
          // time is 13hz from 0.5 to 2.5
          // pitch is 1
          timeMultiplier =
              (std::sin(percent * c_pi * 13.0f) * 0.5f + 0.5f) * 2.0f + 0.5f;
          pitchMultiplier = 1.0f;
        });
    WriteWaveFile("data/out_E_Time.wav", &out, numChannels, sampleRate,
                  numBytes);

    // adjust time and speed on a sine wave
    GranularTimePitchAdjustDynamic(
        source, &out, numChannels, sampleRate, 0.02f, 0.002f,
        [](float percent, float& timeMultiplier, float& pitchMultiplier) {
          // time is 13hz from 0.5 to 2.5
          // pitch is 10hz from 0.75 to 1.25
          timeMultiplier =
              (std::sin(percent * c_pi * 10.0f) * 0.5f + 0.5f) * 2.0f + 0.5f;
          pitchMultiplier =
              1.0f /
              ((std::sin(percent * c_pi * 10.0f) * 0.5f + 0.5f) * 0.5f + 0.75f);
        });
    WriteWaveFile("data/out_E_TimePitch.wav", &out, numChannels, sampleRate,
                  numBytes);
  }

  system("pause");
}
