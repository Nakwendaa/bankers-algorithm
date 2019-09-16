# bankers-algorithm

## Description du programme

Implementation in C of the banker's algorithm, an algorithm that allows to manage the allocation of different types of resources, while avoiding deadlocks.

The code provided implements a client-server model that uses sockets as a means of communication.

On the one hand, the server application is similar to an operating system that manages the allocation of resources, such as memory, hard disk or printers. This server simultaneously receives several resource requests from different clients through connections. For each connection/request, the server must decide when resources can be allocated to the client, in order to avoid deadlocks by following the banker's algorithm.

On the other hand, the client application simulates the activity of several clients in different threads. These clients can request resources if they are available or release resources they currently hold.

## Protocole client-serveur

The communication protocol between the client and the server consists of commands and responses, each of which is a line of text in utf-8 format (and terminated by the ASCII character 10, line feed).

The protocol is managed by the client that makes requests to the server. The server responses are always either ACK or ERR.

### Requêtes du client

BEG nb_resources          Configures the number of resources
PRO rsc0 rsc1...              Provides resources
END                                 Ends server execution

INI max0 max1 ...            Customer ad with maximum usage
REQ rsc0 rsc1 ...             Request for resources
CLO                                 Announces the end of the customer

### Réponses du serveur 

ACK                 Order successfully executed
ERR *msg*       Invalid command, msg explains why

## Usage

Open two terminal windows side by side in the folder bankers-algorithm. Then do ``cd build`` in each window.
In the first window, run the server with:
``tp2_server <port-nb> <nb-threads>``

Example: ``./tp2_server 14648 7``


Then immediately In the second window, run the clients with the following command:&nbsp;
```./tp2_client <port-nb> <nb-clients> <nb-requests> <resources>```

Example: ``./tp2_client 14648 45 39 45``

## License
[MIT](https://raw.githubusercontent.com/Nakwendaa/bankers-algorithm/master/LICENSE)
