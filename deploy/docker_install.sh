echo installing docker
echo removing conflicting packages
for pkg in docker.io docker-doc docker-compose podman-docker containerd runc; do sudo apt-get remove $pkg;done

echo -----------starting docker installation-------------
sudo apt-get update
sudo apt-get install ca-certificates curl gnupg

sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg


echo \
  "deb [arch="$(dpkg --print-architecture)" signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  "$(. /etc/os-release && echo "$VERSION_CODENAME")" stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null


echo ----------INSTALLING DOCKER-------------------------
sudo apt-get install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

echo -------ADDING DOCKER TO UBUNTU GROUP----------------------------
sudo usermod -aG docker ubuntu

echo ---------RESTARTING DOCKER---------------
sudo systemctl restart docker

echo "DOCKER INSTALLED. PLEASE LOG BACK IN FOR CHANGES TO TAKE EFFECT"


echo -----CREATING DOCKER OVERLAY NETWORK DATACHANNEL-------------
