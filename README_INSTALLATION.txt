Program Setup and Compiliation
===========================================================================================
This project uses some of the newer ways to build and link dependencies.
Specifically, it uses [vcpkg.exe] for managing external libraries and packages.


1) Install git
	- https://git-scm.com/
	- make sure git is installed, test in command line:
		- git --version

2) Create a directory where all git projects are located, such as:
	- C:\github

3) Install vcpkg.exe and run it
	- https://github.com/microsoft/vcpkg
	- git clone https://github.com/Microsoft/vcpkg.git 
	- git pull
	- [MUST DO] just follow installation instructions above (url)
		a) Close all Visual Stuido Projects
		b) Run: .\vcpkg\bootstrap-vcpkg.bat
		c) Integrate with VS: .\vcpkg\vcpkg integrate install

4) Run vcpkg.exe to search/list for packages you need
	- vcpkg.exe search
	- vcpkg.exe list

5) Install boost x64/x86
	- vcpkg.exe install boost
	- vcpkg.exe install boost:x64-windows

6) [Optional] install other packages
	- Install any additional packages and libraries as needed 
	- vcpkg.exe install curl openssl mysql cereal

7) Open project and just compile!
	- no need to fiddle with project settings, libs, dependecies, versions, includes, etc... anymore!
	- vcpkg will handle it all for you!