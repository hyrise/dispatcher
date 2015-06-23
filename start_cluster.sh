path_to_hyrise=../hyrise
number_of_cores=4;
number_of_instances=4;
hyrise_port=5000;
path_to_dispatcher=$(pwd)

cd $path_to_hyrise

for ((id=0;id<$number_of_instances;id++))
do
    $path_to_hyrise/build/hyrise-server_debug -l $path_to_hyrise/build/log.properties -p $(($hyrise_port + $id)) --corecount $number_of_cores --coreoffset 0 --nodeId $id --dispatcherport 8888 &
done

$path_to_dispatcher/dispatcher 8888

killall -r hyrise
