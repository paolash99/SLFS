wget https://github.com/apache/openwhisk-cli/releases/download/1.2.0/OpenWhisk_CLI-1.2.0-linux-amd64.tgz
tar -xf OpenWhisk_CLI-1.2.0-linux-amd64.tgz
sudo cp wsk /usr/bin

echo -----CHECKING WSK CLI INSTALLATION-----
wsk --help
