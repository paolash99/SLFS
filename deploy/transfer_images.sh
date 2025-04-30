sudo apt install pv

hosts=(ssbd-3 ssbd-4)
images=(whisk/invoker)

for i in "${images[@]}"; do
	for h in "${hosts[@]}"; do
		docker save "$i" | pv | ssh "$h" docker load ;
	done
	wait < <(jobs -p);
done
