hbtad: hbtad.c
	gcc -o hbtad hbtad.c -lpcap -lm

check-syntax: hbtad.c
	gcc -o hbtad hbtad.c -lpcap -lm
