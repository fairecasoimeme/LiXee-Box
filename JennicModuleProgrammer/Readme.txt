******************************************
* How to build the project in MCUXpresso *
******************************************

1. Install MINGW from https://sourceforge.net/projects/mingw/files/latest/download?source=files
	a. Add MINGW to PATH environment variable

when the last step is done, you can build the project. 

************************************
* How to run ISP and boot campaign *
************************************

1. Install SWIG for Windows from https://sourceforge.net/projects/swig/files/swigwin/swigwin-3.0.12/swigwin-3.0.12.zip/download?use_mirror=10gbps-io
2. Add SWIG to PATH environment variable
3. Install python 2.7.x
	a. Add distutils.cfg file in C:\Python27\Lib\distutils folder. The distutils.cfg file shall contain following lines:
		[build]
		compiler = mingw32

4. Build flash programmer in MCUXpresso
5. In flash programmer’s project, look for \JN5180\tools\JennicModuleProgrammer\Python folder and type under windows command line:
	>python setup.py install

   This script creates a package in python that can be used by other scripts when doing “import jnprog”.


Note that: 
	a. the test_conn*.py scripts in \JN5180\tools\JennicModuleProgrammer\Python folder have not been tested. 
	b. once the setup.py install command is run, the Python folder is populated with temporary files. 
	   If you want to have the test_conn*.py starting to execute (no “import jnprog” issue) make sure to remove in the jnprog folder, the __init__.py* files.

6. In \JN5180\ROM\lpcopen\boot\test\scripts folder, do following:
	a. if the DUT has only one UART, run below command:
	>python test_boot.py COMx (replace x with your value)


	b. if the DUT has two UARTs
	Firts edit the python script and set number_of_ports=2 and is_manual_isp=0 and then run below command:
	>python test_boot.py COMx COMy

	Replace COMx with the value of UART1 (prog_port)
	Replace COMy with the value of UART0 (com_port)
	
7. Test results can be found in \JN5180\ROM\lpcopen\boot\test\scripts in the test.log file.

Before running a new campaign run following commands to remove restrictions that were put on the flash and that will prevent the test from running properly next time:
	>JN518xProgrammer.exe -s COM19 -w PSECT:8@32=0x0000000000000000
	This command is used to reset the ECRP to 0 to remove the restriction on flash

	>JN518xProgrammer.exe -s COM19 -e FLASH:512@650752
	This command is used to reset the restriction on the OTP page that has been applied once it was written once.

