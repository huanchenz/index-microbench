# index-microbench

## Generate Workloads ##

1. Download [YCSB](https://github.com/brianfrankcooper/YCSB/releases/latest)

   ```sh
   curl -O --location https://github.com/brianfrankcooper/YCSB/releases/download/0.11.0/ycsb-0.11.0.tar.gz
   tar xfvz ycsb-0.11.0.tar.gz
   mv ycsb-0.11.0 YCSB
   ```

2. Create Workload Spec

   The default workload a-f are in ./workload_spec

   You can of course generate your own spec and put it in this folder.

3. Modify workload_config.inp

   The workload configuration file is a csv file containing the following two columns:
   
   workload: workload spec name
   keytype: key type (randint = random integer; monoint = monotonically increasing integer; email = email keys with host name reversed)
   
   For each line (except the header line) in the workload configuration file a workload is generated. 
   The generated workload ares stored in the folder ./workloads. 
   Each workload consists of the following two files: ./workloads/<workload spec name>_<key type>_load.dat and ./workloads/<workload spec name>_<key type>_txn.dat

4. Generate

   ```sh
   make generate_workload
   ```

   The generated workload files will be in ./workloads

5. NOTE: To generate email-key workloads, you need an email list (list.txt)

6. Ensure papi is available

e.g. on centos 
```sh
sudo dnf install papi papi-devel
```

7. Build

  1. Using make
```sh
make all 
```

  2. Using cmake

```sh
mkdir build
cd build
cmake ..
make
```

8. Running

To run ensure that your working directory is the root of the index-microbench project.
```sh
<path-to-executable>/workload <workload_name> <index_structure_name>
```

The workload_name is the name of a workload generated using the workload generator. 
The index_structure_name is either art or btree.

