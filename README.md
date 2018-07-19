# mstream
Разрабатывалось и проверялось под Ubuntu 16.04

Для сборки проекта необходимо установить\
cmake\
ffmpeg для разработки (либо собрать https://trac.ffmpeg.org/wiki/CompilationGuide )\
sdl - для отображения видеопотока

mkdir build\
cd build\
cmake -DCMAKE_BUILD_TYPE=Release [mstream_path]\
make

В папку с stream можно положить конфиг mstream/conf/mstream.conf с урлами

ulr 1 <url> - top left\
ulr 2 <url> - top right\
ulr 3 <url> - bottom left\
ulr 4 <url> - bottom right\
\
из командной строки доступны команды \
\
q,quit - выход\
cfg - перечитать конфиг файл\
url - изменить стрим на лету

