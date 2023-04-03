#!/bin/bash

submission=$1
mkdir ./test_dir

echo $submission

# Accept both .tar.gz and .gz or any other gz ;)
# fileNameRegex="assignment2_easy_[0-9]{4}[A-Z]{2}.[0-9]{4}.*.gz"

# if ! [[ $submission =~ $fileNameRegex ]]; then
# 	# not considering zip for now
# 	echo "File doesn't match the naming convention"
# 	exit
# fi

# Extract the Entry number
entryNoRegex="[0-9]{4}[A-Z]{2}.[0-9]{4}"
if [[ $submission =~ $entryNoRegex ]]; then
	entryNum=${BASH_REMATCH[0]}
	echo "Entry Number: $entryNum"
fi

echo "Setting the test directory"

tar -xzvf "$submission" -C ./test_dir
cp assig2_1.c assig2_2.c out* *.sh ./test_dir
cd ./test_dir

FILE=*[Rr][Ee][Pp][Oo][Rr][Tt]*.pdf
if ! ls $FILE 1> /dev/null 2>&1; then
	echo "Report does not exist"
	#exit
fi

adduprogs(){
	# takes the program as argument
	echo "Modifying the Makefile"
	EXTRAUPROGS="UPROGS="
	# For all testcases
	EXTRAUPROGS="$EXTRAUPROGS $1"

	# remove assig2_* files from UPROGS if already present
	sed -i 's/_assig2_.//g' Makefile
	sed -i 's/UPROGS=/'"$EXTRAUPROGS"'/g' Makefile
	make fs.img
	make
}


sed -i 's/_assig2_.//g' Makefile
# Add the test cases to the Makefile


# Change the gcc binary
sed -i 's/gcc-10\|gcc-9/gcc/g' Makefile

echo "Executing the test cases"

pkill qemu-system-x86
pkill qemu-system-i386
make clean

# Executing each test case independently with a timeout of 30 seconds
echo "Running..1"
adduprogs _assig2_1
timeout 30s ./test_assig2.sh assig2_1|grep -i 'The completed process'|sed 's/$ //g' > res_assig2_1

echo "Running..2"
adduprogs _assig2_2
timeout 30s ./test_assig2.sh assig2_2|grep -i 'The completed process'|sed 's/$ //g' > res_assig2_2



check_test=2
total_test=0

echo "" > .output
marks="$entryNum"
for ((t=1;t<=$check_test;++t))
do
	echo -n "Test #${t}: "

	# NOTE: we are doing case insensitive matching.  If this is not what you want,
	# just remove the "-i" flag
	# Ignoring blank spaces
	if diff -iZwB <(cat out_assig2_$t) <(cat res_assig2_$t) > /dev/null
	then
		echo -e "\e[0;32mPASS\e[0m"
		marks="$marks,1" # 1 mark for pass
		((total_test++))
	else
		echo -e "\e[0;31mFAIL\e[0m"
		marks="$marks,0" # 0 mark for fail
		echo "Output for test case $t:" >> .output
		cat res_assig2_$t >> .output
	fi
done
