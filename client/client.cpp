#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <vector>
#include <map>
#include <string>
#include <string.h>
#include <stdexcept>
#include <exception>
#include <limits.h>
#define SERVER_PORT "12345" // Randomowy port
#define MAX_HEADER_BUFF_LEN 256 // Maksymalna dlugosc bufora naglowkow
#define HEADER_END_CHARACTER '\n' // Znak oznaczajacy koniec naglowka
#define DOWNLOAD_REQUEST "downld" // flaga oznaczajaca ze klient chce pobrac cos z serwera
#define UPLOAD_REQUEST "upload" // flaga ze klient chce wrzucic cos na serwer
#define SIZE_HEADER_LENGTH 99
#define TRANSFER_BUFFER_LENGTH 32768 // max dlugosc bufora do transferu danych (wysylanie / odbieranie)
// Enum ktory ulatwia rozpoznawanie co serwer ma zrobic z danym klientem
// WAITING - serwer oczekuje na naglowek z zadaniem (upload / download)1
// DOWNLOAD - klient zazadal pobrania jakiegos pliku z serwera, wysylane sa dane
// UPLOAD - klient zazadal wyslania jakiegos pliku na serwer, odbierane sa dane
enum Request {
	WAITING,
	DOWNLOAD,
	UPLOAD
};
// Funkcja ktora wyswietla menu co uzytkownik chce zrobic
int choice()
{
	std::cout << "Co bys chcial zrobic?" << std::endl;
	std::cout << "1 Wyslij plik na server" << std::endl;
	std::cout << "2 Pobierz plik z servera" << std::endl;
	std::cout << "3 Zakoncz polaczenie" << std::endl;
	int nr = -1;
	try {
		std::cin >> nr;
	}
	catch (std::exception e) {
		std::cout << "Podano zla wartosc!" << std::endl;
		nr = -1;
	}
	return nr;
}

bool sendHeader(int socketFD, Request type, std::string filePath = "", std::string fileSize = "")
{
	// Przygotuj naglowek
	std::string header = (type == Request::UPLOAD) ? UPLOAD_REQUEST : DOWNLOAD_REQUEST;
	if ((type == Request::DOWNLOAD) && !filePath.empty() && !filePath.empty()) {
		header = header + ':' + filePath + HEADER_END_CHARACTER;
	}
	else if ((type == Request::UPLOAD) && !fileSize.empty())
	{
		header = header + ':' + fileSize + HEADER_END_CHARACTER;
	}
	std::cout << "Naglowek: " << header << std::endl;
	// Wyslij naglowek do serwera
	ssize_t sentSize = 0;
	ssize_t tmp = 0;
	while (sentSize < (ssize_t)(header.size())) {
		tmp = send(socketFD, header.c_str() + sentSize, header.size() - sentSize, 0);
		if (tmp == -1) {
			return true;
		}
		sentSize += tmp;
	}
	return false;
}

bool uploadAction(int socketFD)
{
	std::cout << "Prosze podac sciezke do pliku:" << std::endl;
	std::string filePath;
	// Odczytaj sciezke do pliku
	std::cin >> filePath;
	std::cout << "Wysylanie pliku: " << filePath << std::endl;
	// Otworz plik
	FILE* file;
	file = fopen(filePath.c_str(), "r");
	if (file == nullptr) {
		std::cerr << "Nie udalo sie otworzyc pliku ze sciezka: " << filePath << std::endl;
		return true;
	}
	// sprawdzenie rozmiaru pliku
	int fileSize = 0;
	struct stat fileStat;
	if (stat(filePath.c_str(), &fileStat) == 0) {
		// Dodaj rozmiar do naglowka
		fileSize = fileStat.st_size;
	}
	else
	{
		std::cerr << "Nie udalo sie pobrac rozmiaru pliku" << std::endl;
		return true;
	}

	if (sendHeader(socketFD, Request::UPLOAD, "", std::to_string(fileSize))) {
		fclose(file);
		return true;
	}
	char buff[TRANSFER_BUFFER_LENGTH] = { 0 };
	size_t readSize = 0;
	bool fileEnded = false;
	ssize_t sentSize = 0;
	ssize_t tmp = 0;
	while (true) {
		readSize = 0;
		sentSize = 0;
		fileEnded = false;
		readSize = fread(buff, 1, TRANSFER_BUFFER_LENGTH, file);
		if (readSize < TRANSFER_BUFFER_LENGTH) {
			if (feof(file) != 0) {
				fileEnded = true;
			}
			else {
				std::cerr << "Blad przy czytaniu pliku!" << std::endl;
				fclose(file);
				return true;
			}
		}
		// Wyslij caly bufor
		while (sentSize < (ssize_t)(readSize)) {
			printf("%s", buff + sentSize);
			tmp = send(socketFD, buff + sentSize, readSize - sentSize, 0);
			if (tmp < 0) {
				std::cerr << "Blad przy wysylaniu danych pliku do serwera" << std::endl;
				fclose(file);
				return true;
			}
			sentSize += tmp;
		}
		if (fileEnded)
		{
			sentSize += tmp;
			return false;
			fclose(file);
		}
	}
}
bool downloadAction(int socketFD)
{
	std::cout << "Prosze podac nazwe pliku do pobrania" << std::endl;
	std::string fileName;
	try {
		std::cin >> fileName;
	}
	catch (std::exception& e) {
		std::cerr << "Cos poszlo nie tak z pobieraniem nazwy pliku" << std::endl;
	}
	// Otworz plik
	FILE* file;
	file = fopen(fileName.c_str(), "w");
	if (file == nullptr) {
		std::cerr << "Nie udalo sie stworzyc pliku" << fileName << std::endl;
		return true;
	}
	// Wysylany jest naglowek do serwera za pomoca funkcji sendHeader
	if (sendHeader(socketFD, Request::DOWNLOAD, fileName)) {
		fclose(file);
		return true;
	}
	char headerSize[SIZE_HEADER_LENGTH] = { 0 };
	ssize_t receivedSize = 0;
	// Otrzymywany jest naglowek z serwera z rozmiarem pliku
	receivedSize = recv(socketFD, headerSize, SIZE_HEADER_LENGTH, 0);
	if (receivedSize <= 0) {
		if (receivedSize == 0) {
			std::cout << "Server zamknal polaczenie" << std::endl;
		}
		else {
			std::cerr << "Blad przy otrzymywaniu danych z serwera" << std::endl;
		}
		fclose(file);
		return true;
	}
	if (receivedSize > SIZE_HEADER_LENGTH) {
		std::cerr << "Otrzymany naglowek ma niepoprawna dlugosc" << std::endl;
		fclose(file);
		return true;
	}
	std::string sizeString = std::string(headerSize).substr(std::string(headerSize).find("SIZE:") + 5);
	std::cout << "Rozmiar pliku: " << sizeString << std::endl;
	size_t fileSize = std::stoul(sizeString);
	char buff[TRANSFER_BUFFER_LENGTH] = { 0 };
	receivedSize = 0;
	while (fileSize > 0) {
		receivedSize = recv(socketFD, buff, (TRANSFER_BUFFER_LENGTH < fileSize ? TRANSFER_BUFFER_LENGTH : fileSize), 0);
		if (receivedSize <= 0) {
			if (receivedSize == 0) {
				std::cout << "Server zamknal polaczenie" << std::endl;
			}
			else {
				std::cerr << "Blad przy otrzymywaniu danych z serwera" << std::endl;
			}
			fclose(file);
			return true;
		}
		fileSize -= receivedSize;
		fwrite(buff, sizeof(char), receivedSize, file);
	}
	fclose(file);
	std::cout << "Plik zostal pobrany poprawnie" << std::endl;
	return false;
}
int main()
{
	// Najpierw tworzone jest gniazdo i probowane jest polaczenie z serwerem
	struct addrinfo hints = { 0 };
	struct addrinfo* res = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	int status;
	status = getaddrinfo(nullptr, SERVER_PORT, &hints, &res);
	if (status != 0) {
		perror("Blad przy pobieraniu info o serwerze");
		return -1;
	}
	int socketFD;
	socketFD = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (socketFD == -1) {
		std::cerr << "Blad przy tworzeniu socketa" << std::endl;
		freeaddrinfo(res);
		return -1;
	}
	status = connect(socketFD, res->ai_addr, res->ai_addrlen);
	if (status == -1) {
		std::cerr << "Error przy connect() - polaczeniu do servera" << std::endl;
		close(socketFD);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	bool errorHappened = false;
	while (!errorHappened) {
		switch (choice()) {
		case 1:
			errorHappened = uploadAction(socketFD);
			break;
		case 2:
			errorHappened = downloadAction(socketFD);
			break;
		case 3:
			errorHappened = true;
			break;
		default:
			errorHappened = true;
			break;
		}
	}
	close(socketFD);
	return 0;
}