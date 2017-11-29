dispatcher
==========

#### Simple Query dispatcher for distributed Hyrise cluster
The dispatcher uses a settings file for the configuration. The settings file contains the information about the hosts and for the start script, see below, additional information like the path of the executables for the dispatcher and the hyrise database.

#### Creating a HYRISE cluster
In order to help the developer creating a cluster, this repository contains a python script with the name `start_cluster.py`. The python script uses the same settings file that is used by the dispatcher.

#### Installation

First check if the submodule http-parser is initialized correctly:

    git submodule update --init

A simple build is executed by

	make


[ ![Codeship Status for hyrise/dispatcher](https://codeship.com/projects/5f42f5b0-4e4e-0132-79c9-32a961e53655/status)](https://codeship.com/projects/47634)
