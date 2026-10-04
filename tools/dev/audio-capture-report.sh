#!/usr/bin/env bash
# Numbers and a picture for an audio capture (a .wav written by UC2ALAudio through OpenAL Soft's wave writer):
# duration, overall peak/RMS (ffmpeg astats), RMS per second, the share of silent seconds, a spectral
# summary (energy per octave band) and a spectrogram PNG beside the capture.
#
#   tools/dev/audio-capture-report.sh <capture.wav>      -> <capture>.png, numbers on stdout
#
# Runs ffmpeg under <WorkUC2>/bin/capped (CLAUDE.md, "Running anything").
set -u
wav=${1:?capture.wav}
here=$(cd "$(dirname "$0")/../.." && pwd)
capped="$here/../bin/capped"
png=${wav%.wav}.png
"$capped" 1G 300 -- ffprobe -v error -show_entries stream=sample_rate,channels,duration -of default=nw=1 "$wav" | tr '\n' ' '; echo
"$capped" 1G 300 -- ffmpeg -nostdin -hide_banner -i "$wav" -af astats=metadata=0:measure_perchannel=none -f null - 2>&1 \
  | grep -a -E "Peak level dB|RMS level dB|RMS peak dB|Flat factor|Peak count" | sed 's/^\[[^]]*\] /  /'
# RMS per second (dBFS); a second below -70 dBFS counts as silent.
"$capped" 1G 300 -- ffmpeg -nostdin -hide_banner -i "$wav" \
  -af "aresample=48000,asetnsamples=n=48000:p=0,astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.RMS_level:file=-" \
  -f null - 2>/dev/null | grep -a -o "RMS_level=.*" | cut -d= -f2 > "${wav%.wav}.rms.txt"
awk '{v=($1=="-inf")?-150:$1; n++; if(v<-70) s++; printf "%s%d:%.0f", (n>1?" ":""), n-1, v} END{printf "\n  seconds: %d, silent (< -70 dBFS): %d\n", n, s}' "${wav%.wav}.rms.txt" | fold -w 120 | sed 's/^/  /'
# Energy per octave band over the whole capture (dB relative to the loudest band).
for f in 63 125 250 500 1000 2000 4000 8000 16000; do
  lo=$(awk -v f=$f 'BEGIN{printf "%d", f/1.414}'); hi=$(awk -v f=$f 'BEGIN{printf "%d", f*1.414}')
  r=$("$capped" 1G 300 -- ffmpeg -nostdin -hide_banner -i "$wav" -af "highpass=f=$lo:poles=2,lowpass=f=$hi:poles=2,astats=metadata=0:measure_perchannel=none" -f null - 2>&1 | grep -a "RMS level dB" | tail -1 | awk '{print $NF}')
  printf "  band %5d Hz: %s dBFS\n" $f "$r"
done
"$capped" 1G 300 -- ffmpeg -nostdin -hide_banner -v error -y -i "$wav" -lavfi "showspectrumpic=s=1600x600:legend=1:scale=log:fscale=log:stop=20000" "$png" && echo "  spectrogram: $png"
