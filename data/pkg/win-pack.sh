#!/bin/bash
# git revision detection adapted from https://github.com/rpavlik/cmake-modules/blob/master/GetGitRevisionDescription.cmake.in

find_gitrev() {
	# output variable
	gitrev="error"

	local gitfolder githead ref reffile
	gitfolder="$1/.git"
	githead="$(cat "$gitfolder/HEAD" 2>/dev/null)"
	if [[ $? -ne 0 ]]; then
		echo "Can't find .git directory"
		return 1
	fi

	if [[ "$githead" =~ ref: ]] ; then
		ref="${githead:5}"
		reffile="$gitfolder/$ref"
		if [[ -e "$reffile" ]]; then
			gitrev="$(cat "$reffile")"
		else
			gitrev="$(grep "$ref" "$gitfolder/packed-refs")"
			if [[ $? -ne 0 ]]; then
				gitrev="unknown"
			else
				gitrev="${gitrev:0:40}"
			fi
		fi
	else
		gitrev="$githead"
	fi
	return 0
}

cmake="$1"
repo="$2"
shortrev=""

if [[ ! -e "$cmake" ]] ; then
	echo "Usage: win-pack.sh /path/to/cmake [/path/to/software/repository]"
	echo "Must run from the build bin directory!"
	return 1
fi

if [[ -n "$repo" ]]; then
	find_gitrev "$repo"
	if [[ $? -eq 0 ]]; then
		shortrev="-${gitrev:0:8}"
	else
		echo "WARNING: failed to determine git revision!"
	fi
fi

outfile="ra-win${shortrev}.7z"
"${cmake}" -E tar cf "${outfile}" --format=7zip -- *.dll *.exe icudtl.dat *_blob.bin config data platforms
echo "Packed ra as $outfile"
