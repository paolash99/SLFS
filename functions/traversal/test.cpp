

int main(int argc, char* argv[])
{
    std::string ip = HostnameToIP("localhost");
    IPAndPort ip_and_port;
    bool stun_request_succesfull =
        PerformStunRequest(ip, 3478, 9910, ip_and_port);
    if (stun_request_succesfull)
    {
        printf("local ip:port = %s:%d\n", ip_and_port.ip_.c_str(),
               ip_and_port.port_);





    }
    else
    {
        printf("Failed to perform stun request");
    }
}
