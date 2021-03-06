# ODC Examples

ODC provides several examples of [DDS topologies](http://dds.gsi.de/doc/nightly/topology.html) which are installed in `INSTALL_DIR/share/odc`.

## Run locally

Start ODC gRPC server:
```
> odc-grpc-server
```

Start ODC gRPC client:
```
> odc-grpc-client
```

Alternatively, start the CLI server:
```
> odc-cli-server
```

By default this example uses [localhost plugin](http://dds.gsi.de/doc/nightly/RMS-plugins.html#localhost-plugin) of [DDS](https://github.com/FairRootGroup/DDS) and three topologies which are installed in `INSTALL_DIR/share/odc/`:

* `ex-dds-topology-infinite.xml` - initial topology with 12 FairMQ devices;
* `ex-dds-topology-infinite-up.xml` - upscaled topololy with 36 FairMQ devices;
* `ex-dds-topology-infinite-down.xml` - downscaled topology with 24 FairMQ devices.

## Run on ALICE HLT cluster

Installation of ODC and its dependencies using [aliBuild](https://github.com/alisw/alibuild):

```
> mkdir INSTALL_DIR
> cd INSTALL_DIR
> git clone https://github.com/alisw/alidist.git
> aliBuild --default odc build ODC
```

For this example we use [SSH plugin](http://dds.gsi.de/doc/nightly/RMS-plugins.html#ssh-plugin) of [DDS](https://github.com/FairRootGroup/DDS) which requires a SSH configuration file.

Create file `hosts.cfg` with the following content:

```
@bash_begin@
export PATH=/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/opt/ibutils/bin:$PATH
cd INSTALL_DIR/
eval `alienv --no-refresh load ODC/latest-odc`
cd -
@bash_end@

wn_epn-vs000, epn-vs000, , WRK_DIR,  12
wn_epn-vs001, epn-vs001, , WRK_DIR,  12
wn_epn-vs002, epn-vs002, , WRK_DIR,  12
```

Replace `INSTALL_DIR` and `WRK_DIR` with the real directory names. Important prerequisite: working directories `WRK_DIR` from SSH config file must exist. We recommend to use the local storage of the machine (i.e. `/tmp`) for the working directories.

Open two terminal windows. In each of the terminal first initialize the environment:
```
> cd INSTALL_DIR
> alienv enter ODC/latest-odc
> export LC_ALL=C; unset LANGUAGE
> cd INSTALL_DIR/sw/slc7_x86-64/DDS/latest
> source DDS_env.sh
```

Start ODC gRPC server:
```
> odc-grpc-server --rms ssh --config PATH_TO_HOSTS/hosts.cfg --agents 3 --slots 12
```

Start ODC gRPC client:
```
> odc-grpc-client --topo INSTALL_DIR/share/odc/ex-dds-topology-infinite.xml --uptopo INSTALL_DIR/share/odc/ex-dds-topology-infinite-up.xml --downtopo INSTALL_DIR/share/odc/ex-dds-topology-infinite-down.xml
```

### Standard sequence of requests
```
.init
.submit
.activate
.config
.start
.stop
.upscale (Scale topology up. From 12 to 36 devices.)
.start
.stop
.downscale (Scale topology down. From 36 devices to 24.)
.start
.stop
.reset
.term
.down
.quit
```

## Create a DDS topology

[The DDS topology example](src/odc-topo.cpp) shows how to create a topology XML file using [DDS topology APIs](https://github.com/FairRootGroup/DDS/tree/master/dds-topology-lib/src). In the example we combine two topologies [1](ex-dpl-topology.xml) and [2](ex-dd-topology.xml) into a single XML file. The first topology is an example of the DPL export to DDS. The second one is an example of TfBuilder task declaration. 

We create a "EPNGroup" group and add it into the main group of the topology. Set required number of  collections. Read DPL collection from XML file and add it to "EPNGroup" group. Add TfBuilder task, which is read from XML file, into a DPL collection. Save the topology to a file.
