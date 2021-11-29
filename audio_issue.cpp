#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <iostream>

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
// cleanup & error checks are intentionally skipped
int main(int argc, char **argv){
        
    InputParser input(argc, argv);
    if(input.cmdOptionExists("-h")){
        std::cout <<" Choose -a for 24 fps -b for 25 fps"<<std::endl;
    }
    if(input.cmdOptionExists("-a")){
        fps = 24000;
    }
    if(input.cmdOptionExists("-b")){
        // Do stuff
        fps = 25000;
    }
    if (fps==0){
        std::cout <<" Choose -a for 24 fps -b for 25 fps"<<std::endl;
        exit(1);
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
            std::cout <<timeValue<<" == "<<timeScale<<std::endl;
            displayMode->GetFrameRate(&timeValue, &timeScale);
            if (displayMode->GetFieldDominance() == bmdProgressiveFrame && timeValue == 1000) {
                if ( timeScale == fps)
                {
                    std::cout <<"Selected "<<timeValue<<" == "<<timeScale<<std::endl;
                    break; //
                }
                else {
                    std::cout <<"Skipping "<<timeValue<<" == "<<timeScale<<std::endl;
                }
            }
    }
    deckLinkOutput->EnableVideoOutput(displayMode->GetDisplayMode(), bmdVideoOutputFlagDefault);
    deckLinkOutput->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, ch_count,
                    bmdAudioOutputStreamContinuous);

    IDeckLinkMutableVideoFrame *f = nullptr;
    deckLinkOutput->CreateVideoFrame(displayMode->GetWidth(), displayMode->GetHeight(), displayMode->GetWidth() * 2,
                    bmdFormat8BitYUV, bmdFrameFlagDefault, &f);
    char *audio = get_sine_signal(sample_rate, bps, ch_count, 1000, -18);
    char *audio_start = audio;
    char *audio_end = audio + sample_rate * bps * ch_count;

    auto t0 = high_resolution_clock::now();
    const uint32_t sampleFrameCount = sample_rate * timeValue / timeScale;
    assert(sample_rate % sampleFrameCount == 0);
    long  frame_count = 0;
    long underflow_count = 0;
    long overflow_count = 0;
    while (1) {
            high_resolution_clock::time_point now;
            do {
                    now = high_resolution_clock::now();
            } while (duration_cast<std::chrono::duration<double> >(now - t0).count() < (double) timeValue / timeScale);
            
            t0 = now;

            deckLinkOutput->DisplayVideoFrameSync(f);

            uint32_t sampleFramesWritten = 0;
            uint32_t buffered = 0;
            deckLinkOutput->GetBufferedAudioSampleFrameCount(&buffered);
            if (buffered == 0) {
                    underflow_count = underflow_count + 1;
                    // Skip first one as will always be empty
                    if (frame_count !=0 ){
                        clog << "audio buffer underflow!\n";
                        clog << "at frame " << frame_count << " overflows "<< overflow_count<<" "<<" underflows "<<underflow_count<<std::endl;
                    }
            }
            deckLinkOutput->WriteAudioSamplesSync(audio, sampleFrameCount,
                            &sampleFramesWritten);
            if (sampleFramesWritten < sampleFrameCount) {
                    overflow_count = overflow_count + 1;

                    clog << "audio buffer overflow!\n";
                    clog << "at frame " << frame_count << " overflows "<< overflow_count<<" "<<" underflows "<<underflow_count<<std::endl;
             }
            audio += sampleFrameCount * bps * ch_count;
            if (audio == audio_end) {
                    audio = audio_start;
            }
            frame_count = frame_count + 1;
        }
}
