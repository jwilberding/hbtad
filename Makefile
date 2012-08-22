hbtad: hbtad.c
	gcc -o hbtad hbtad.c -lpcap

check-syntax: hbtad.c
	gcc -o hbtad hbtad.c -lpcap
