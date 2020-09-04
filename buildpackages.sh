#!/usr/bin/env bash
sudo docker rm $(sudo docker ps -qa)
sudo docker rmi $(sudo docker image ls -qa)
currentuser=$USER
echo $currentluser
for i in ./Dockerfile.*
do
    if [ -f "$i" ];
    then
        printf "Filename: %s\n" "${i##*/}" # longest prefix removal
        printf "Extension: %s\n"  "${i##*.}"
		ext=${i##*.}
		
		mkdir -p distribution/$ext
		sudo chown root:root distribution/$ext
		sudo docker build -f ${i##*/} -t charming-${i##*.} .
		sudo docker create -ti --name dummy charming-${i##*.} bash
		sudo docker cp dummy:/package distribution/${i##*.}
		sudo docker rm -f dummy 
		sudo find distribution -name '*.deb' -exec bash -c 'mv $0 "${0/Linux/'$ext'}" && cp "distribution/'$ext'/package/"*.deb distribution' {} \; 
		sudo chown $currentuser distribution/*.*
		printf "\n\n"
    fi
done