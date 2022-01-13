#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <iostream>
#include <algorithm>

#include <vector>
#include "DeckLinkAPI.h"

using namespace std::chrono;
using std::clog;

/// @retval 1 sec of sine wave
static char *get_sine_signal(int sample_rate, int bps, int channels, int frequency, double volume) {
    auto *data = new int16_t[sample_rate * channels * bps]; // allocate one sec
    double scale = pow(10.0, volume / 20.0) * sqrt(2.0);

    for (int i = 0; i < sample_rate; i += 1) {
        for (int channel = 0; channel < channels; ++channel) {
                int val = round(sin(((double) i / ((double) sample_rate / frequency)) * M_PI * 2. ) * ((1ll << (bps * 8)) / 2 - 1) * scale);
                data[i * channels + channel] = val;
        }
    }

    return reinterpret_cast<char *>(data);
}


class InputParser{
    public:
        InputParser (int &argc, char **argv){
            for (int i=1; i < argc; ++i)
                this->tokens.push_back(std::string(argv[i]));
        }
        const std::string& getCmdOption(const std::string &option) const{
            std::vector<std::string>::const_iterator itr;
            itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
            if (itr != this->tokens.end() && ++itr != this->tokens.end()){
                return *itr;
            }
            static const std::string empty_string("");
            return empty_string;
        }
        bool cmdOptionExists(const std::string &option) const{
            return std::find(this->tokens.begin(), this->tokens.end(), option)
                   != this->tokens.end();
        }
    private:
        std::vector <std::string> tokens;
};



const int bps = 2;
const int ch_count = 2;
const int sample_rate = 48000;

int fps = 24000;
int verbose = 0;
// cleanup & error checks are intentionally skipped
int main(int argc, char **argv){
        
    InputParser input(argc, argv);
    if(input.cmdOptionExists("-h")){
        std::clog <<" Choose -a for 24 fps -b for 25 fps -v for verbose"<<std::endl;
    }
    if(input.cmdOptionExists("-a")){
        fps = 24000;
    }
    if(input.cmdOptionExists("-b")){
        // Do stuff
        fps = 25000;
    }

    if (fps==0){
        std::clog <<" Choose -a for 24 fps -b for 25 fps"<<std::endl;
        exit(1);
    }
    if(input.cmdOptionExists("-v")){
        verbose = 1;
    }
    IDeckLinkIterator *deckLinkIterator = CreateDeckLinkIteratorInstance();
    IDeckLink *deckLink = nullptr;
    IDeckLinkOutput *deckLinkOutput = nullptr;
    BMDTimeValue timeValue = 0;
    BMDTimeScale timeScale = 0;
    IDeckLinkDisplayModeIterator *displayModeIterator = nullptr;
    IDeckLinkDisplayMode *displayMode = nullptr;

    deckLinkIterator->Next(&deckLink); // use first device
    deckLink->QueryInterface(IID_IDeckLinkOutput, (void **) &deckLinkOutput);
    deckLinkOutput->GetDisplayModeIterator(&displayModeIterator);
    while (displayModeIterator->Next(&displayMode) == S_OK) {
            displayMode->GetFrameRate(&timeValue, &timeScale);
            std::clog <<timeValue<<" == "<<timeScale<<std::endl;
            if (displayMode->GetFieldDominance() == bmdProgressiveFrame && timeValue == 1000) {
                if ( timeScale == fps)
                {
                    std::clog <<"Selected "<<timeValue<<" == "<<timeScale<<std::endl;
                    break; //
                }
                else {
                    std::clog <<"Skipping "<<timeValue<<" == "<<timeScale<<std::endl;
                }
            }
    }
    deckLinkOutput->EnableVideoOutput(displayMode->GetDisplayMode(), bmdVideoOutputFlagDefault);
    deckLinkOutput->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, ch_count,
                    bmdAudioOutputStreamContinuous);

    IDeckLinkMutableVideoFrame *f = nullptr;
    deckLinkOutput->CreateVideoFrame(displayMode->GetWidth(), displayMode->GetHeight(), displayMode->GetWidth() * 2,
                    bmdFormat8BitYUV, bmdFrameFlagDefault, &f);
    
    BMDTimeScale hardware_time =0;
    BMDTimeScale timeInFrame =0;
    BMDTimeScale ticksPerFrame =0;
    
    BMDTimeScale last_hardware_time =0;
    BMDTimeScale last_timeInFrame =0;
    BMDTimeScale last_ticksPerFrame =0;
    deckLinkOutput->GetHardwareReferenceClock(timeScale, &hardware_time, &timeInFrame, &ticksPerFrame);
    clog << "hardware_time= "<<hardware_time<<" timeInFrame= " <<timeInFrame<<" ticksPerFrame= "<<ticksPerFrame<<std::endl;
    last_timeInFrame = timeInFrame;
    last_hardware_time = hardware_time;
    last_ticksPerFrame = ticksPerFrame;

    char *audio = get_sine_signal(sample_rate, bps, ch_count, 1000, -18);
    char *audio_start = audio;
    char *audio_end = audio + sample_rate * bps * ch_count;

    auto BMDTimeValue t0 = 0;
    BMDTimeValue blk_hardware_time;
    BMDTimeValue blk_timeInFrame;
    BMDTimeValue blk_ticksPerFrame;
    
    deckLinkOutput->GetHardwareReferenceClock(timeScale, &t0, &blk_timeInFrame, &blk_ticksPerFrame);

    const uint32_t sampleFrameCount = sample_rate * timeValue / timeScale;
    assert(sample_rate % sampleFrameCount == 0);
    long  frame_count = 0;
    long underflow_count = 0;
    long overflow_count = 0;
    
    long long clock_calls = 0;
    long long last_clock_calls = 0;
    

    
    deckLinkOutput->GetHardwareReferenceClock(timeScale, &blk_hardware_time, &blk_timeInFrame, &blk_ticksPerFrame);
    
    while (1) {
        BMDTimeValue now=0;
        clock_calls =0;
        do {
                clock_calls = clock_calls +1;
                deckLinkOutput->GetHardwareReferenceClock(timeScale, &now, &blk_timeInFrame, &blk_ticksPerFrame);
        } while (now - t0 < blk_ticksPerFrame);
        if(now -t0 != 1000) {
            clog << "blk_ticksPerFrame " << blk_ticksPerFrame  << " blk_timeInFrame "<< blk_timeInFrame << " now " <<now<<" t0 "<<t0<<" diff "<<now -t0<<std::endl;
        }
        clog << "clock calls "<< clock_calls << " diff "<<clock_calls - last_clock_calls<<std::endl;
        last_clock_calls = clock_calls;
        t0 = now;

        frame_count = frame_count + 1;
        if (frame_count % (fps /1000) == 0){
            clog << frame_count <<std::endl;
        }
    }
}
