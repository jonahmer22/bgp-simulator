i=0
while :; do
	i=$((i + 1))
	echo "=== RUN $i ==="
	if ! make bench; then
		echo "FAILURE ON RUN $i"
		break
	fi
done
