Module: 351 CS 2104 — Operating Systems
Semester: III
Student: Andrew Rashid Moses
Registration Number: 

EduOS is an educational operating system simulator developed using C and Python.
The project demonstrates important operating system concepts such as process management, threading, inter-process communication (IPC), and CPU scheduling.

Prerequisites
note: this c program system i have been using Visual Studio Code for running and writing c language to my adavantage

Before running the project, install the following:

GCC compiler with POSIX support
Python 3.8 or later
Required Python packages
Valgrind (optional for memory checking)
Installation Commands
sudo apt install build-essential
python3 --version
pip install -r python_scheduler/requirements.txt
sudo apt install valgrind
Build and Run Instructions
Part 2 — C Core
cd c_core/
Visual Studio Code  #if prefered 

# Build and run the simulator
make
./eduos

# Run race condition demonstration
make race

# Run fixed version with mutex
make fixed

# Check memory usage with Valgrind
make memcheck

# Remove build files
make clean
Part 3 — Python Scheduler
cd python_scheduler/

pip install -r requirements.txt

# Run using random processes
python3 scheduler_sim.py --random 10 --seed 42

# Run using CSV input file
python3 scheduler_sim.py --file sample_processes.csv

# Round Robin scheduling
python3 scheduler_sim.py --random 6 --quantum 3

# Thread mode
python3 scheduler_sim.py --random 8 --mode thread
Part 4 — Integration Controller
cd controller/

python3 main_controller.py
 
