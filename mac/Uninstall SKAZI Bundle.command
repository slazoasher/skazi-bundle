#!/bin/bash
# Removes everything the SKAZI Bundle installer put on this Mac (asks for your password).
echo "This removes all SKAZI Bundle plugins and apps."
read -r -p "Continue? (y/n) " answer
[ "$answer" = "y" ] || exit 0

for name in "SKAZI" "SHAKSUKA" "PITA PUMP" "The FilterFish" "SUBICH" "BAMBA" "TAHINI" "ZHUG" "BULLY" "KICKASS" "SALAT" "SKAZI 303" "MIDI GOD" "KUBE" "QUAZI" "SKACID" "SKILTER" "SKWIDE" "SKAZI REVERB" "S-QUALIZER" "SKAZON"; do
    sudo rm -rf "/Library/Audio/Plug-Ins/Components/$name.component"
done
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/SKAZI"
for id in com.skazi.bundle.vst3 com.skazi.bundle.au com.skazi.bundle.apps; do
    sudo pkgutil --forget "$id" >/dev/null 2>&1
done
killall -9 AudioComponentRegistrar >/dev/null 2>&1
sudo rm -rf "/Applications/SKAZI"
echo "SKAZI Bundle removed."
