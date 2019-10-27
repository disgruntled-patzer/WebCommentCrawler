/**************************************************************************************

CS3103 Assignment 1 Part C
Lau Yan Han, A0164750E

This parallel webcrawler implementation acts on a pre-specified list of urls, parses through them
for additional urls, and repeated the process for a specified number of iterations

Brief overview:

	- The main function initiates the initial url list and curl, and calls the Web Crawler class main function
	- The Web Crawler main function (manage_web_crawler) opens the main text file where urls will be written to
	- manage_web_crawler initiates the following:
		1. Talk to the websites in the url list and retrieve data from them
		2. Save the urls into the text file, and clear the url list for later use
		3. Parse through the retrieved data, extract urls, and add them to the (same) url list
		4. Repeat 1 - 3 for a specified number of iterations

For the application of this project, two iterations will be run. The first iteration crawls through a
Youtube channel and extracts the urls to its videos. The second iteration crawls through each video's page,
looks at the recommended videos, finds the url of the users who upload those videos, and extract those urls

Sources:
https://github.com/jarvis57/Web-Crawler
https://github.com/MelvinKool/WebCrawler
https://curl.haxx.se/libcurl/c/example.html
https://stackoverflow.com/questions/9786150/save-curl-content-result-into-a-string-in-c

*************************************************************************************/

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <string.h>
#include <vector>
#include <unordered_set>
#include <curl/curl.h>
#include <sys/time.h>
#include <unistd.h>

// Min response time for a given url in seconds. Responding too quickly means the URL is not valid
// Based on test runs, invalid websites give a response time 5e-5 seconds (0.005ms); set the threshold slightly higher
#define MIN_RESPONSE_TIME 1e-4

// Max number of iterations that the web crawler will run the main loop for.
#define MAX_ITER 2

/**************************************************************************************

Main class to handle web crawler

**************************************************************************************/

class Crawler{

	private:
		std::vector <std::string> urllist, recv_buffer; // urllist = temporary holding place for a list of urls, recv_buffer = raw data received from websites
		std::vector<double> url_response_time; // Reponse time of each url in seconds
		std::unordered_set<std::string> url_lookup; // Lookup table to make sure there are no double url entries in the final text tile
		std::ofstream filehandler; // Text file handler to save discovered urls into text file

		/* Called by the CURL handlers to write any data received from websites to the recv_buffer */
		static size_t write_content(void *content, size_t size, size_t nmemb, void *ptr){
			((std::string*)ptr)->append((char*)content, size * nmemb);
			return size * nmemb;
		}

	public:

		/**************************************************************************************

		Constructor to fill up the initial url

		**************************************************************************************/
	
		Crawler(std::string initial_url){
			urllist.push_back(initial_url);
		}

		/**************************************************************************************
		
		Run the CURL multi handler to receive data from websites mentioned in urllist

		**************************************************************************************/
		void talk_to_multiple_websites(){
			
			if (urllist.empty()){
				return;
			}
			
			// Variables for curl multi-handling
			CURLM *multi_handle;
			std::vector<CURL*> url_handles;
			int still_running = 0;
			int urllist_len = urllist.size();

			// Temporary holding variables to fill the url_handles and recv_buffer vectors
			CURL *temp_handle;
			std::string temp_recv_buffer;
			int i;

			multi_handle = curl_multi_init();

			// For some reason, performing recv_buffer.push_back together with all the operations in the next loop
			// results in a memory overflow, so this is broken out into a separate loop
			for (i = 0; i < urllist_len; ++i){
				recv_buffer.push_back(temp_recv_buffer);
			}
			
			// Fill up the url_handles and set all options accordingly
  			for (i = 0; i < urllist_len; ++i){
				std::cout << "Obtaining data from " << urllist[i].c_str() << std::endl;
    			url_handles.push_back(temp_handle);
    			url_handles[i] = curl_easy_init();
    			curl_easy_setopt(url_handles[i], CURLOPT_URL, urllist[i].c_str());
    			curl_easy_setopt(url_handles[i], CURLOPT_WRITEFUNCTION, write_content);
    			curl_easy_setopt(url_handles[i], CURLOPT_WRITEDATA, &recv_buffer[i]);
    			curl_multi_add_handle(multi_handle, url_handles[i]);
  			}

			// Run the multi-handle. Curl will get the web pages for us (:
			curl_multi_perform(multi_handle, &still_running);

			// The following loop keeps the curl_multi_perform running as long as there are uncompleted handles
			// Code is taken from https://curl.haxx.se/libcurl/c/multi-double.html
			while(still_running) {
				struct timeval timeout;
				long curl_timeout = -1;
				int rc, maxfd = -1; // rc = select() return code, maxfd represents how many file descriptors are ready
				CURLMcode mc; // curl_multi_fdset() return code
				
				fd_set fdread, fdwrite, fdexcep;

				FD_ZERO(&fdread);
				FD_ZERO(&fdwrite);
				FD_ZERO(&fdexcep);

				// Set the timeout value to 1 sec
				timeout.tv_sec = 1;
				timeout.tv_usec = 0;
				curl_multi_timeout(multi_handle, &curl_timeout);
				if(curl_timeout >= 0) {
					timeout.tv_sec = curl_timeout / 1000;
					if(timeout.tv_sec > 1){
						timeout.tv_sec = 1;
					}
					else{
						timeout.tv_usec = (curl_timeout % 1000) * 1000;
					}
				}

				// Get file descriptors from the transfers
				mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
				if(mc != CURLM_OK) {
					std::cout << "curl_multi_fdset() failed, code " << mc << std::endl;;
					break;
				}

				// If maxfd == -1, there are no file descriptors ready yet so we sleep for 100ms
				// Otherwise, we call select(maxfd + 1, ...) to check the file descriptors
				if(maxfd == -1) {
					#ifdef _WIN32 // For windows systems
						Sleep(100);
						rc = 0;
					#else // For nonwindows systems
						struct timeval wait = { 0, 100 * 1000 }; // 100ms
						rc = select(0, NULL, NULL, NULL, &wait);
					#endif
				}
				else {
					rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
				}
				switch(rc) {
					case -1:
						break; // Select Error
					case 0:
					default:
						curl_multi_perform(multi_handle, &still_running);
						break;
				}
			}

			// Obtain the response time, then cleanup
			for (i = 0; i < urllist_len; ++i){
				url_response_time.push_back(-1);
				curl_easy_getinfo(url_handles[i], CURLINFO_TOTAL_TIME, &url_response_time[i]);
			}
			curl_multi_cleanup(multi_handle);
			for (i = 0; i < urllist_len; ++i){
				curl_easy_cleanup(url_handles[i]);
			}

		}

		/**************************************************************************************
		
		Save the urls in urllist to the text file + url_lookup hash table, then clear the urllist.
		This function should be called immediately after curl multi handler talks to the urls, so that
		the urllist can be cleared to accomodate new incoming urls when parsing through acquired data

		**************************************************************************************/
		void save_urls_to_file(int iter){
			
			if (urllist.empty()){
				return;
			}
			std::stringstream ss;
			for (int i = 0; i < urllist.size(); ++i){
				
				// The last call of this function does not get the response time of the urls
				if (iter < MAX_ITER){
					// If response time is too small, it means the link is invalid
					if (url_response_time[i] < MIN_RESPONSE_TIME){
						continue;
					}

					// A stringstream is used to compile all relevant info and send it to the filehandler
					// The url is inserted into the lookup table, and the stringstream is cleared for the next iteration
					ss << urllist[i].c_str() << ", " << 100 * url_response_time[i] << " ms\n";
				}
				else{
					ss << urllist[i].c_str() << ", 0 ms\n";
				}
				
				filehandler << ss.str();
				url_lookup.insert(urllist[i]);
				ss.str(std::string());
			}
			urllist.clear();
			url_response_time.clear();
		}

		/**************************************************************************************
		
		Parse through each data in the recv_buffer using parser tools, then save all detected urls into the
		urllist (to be handled by curl multi handler later). Duplicate urls will be ignored (using the
		url_lookup and the temp_url_lookup hash tables, which speed up the checking of duplicates)
		
		2 iterations are carried out, the iteration number is determined by the "iter" variable
		Which url type this function extracts is determined by the iteration number
		
		Credits to https://github.com/jarvis57/Web-Crawler for the skeleton parser code

		**************************************************************************************/
		void parse_through_acquired_data(int iter){
			
			std::string url_prefix = "https://www.youtube.com";
			std::string temp_url;

			// A second lookup table to store urls that are extracted immediately from the acquired data
			// This second table is neccessary to make sure any duplicates WITHIN the acquired data are eliminated
			// before the next iteration of accessing these urls.
			std::unordered_set<std::string> temp_url_lookup;

			// URL's possible prefixes and suffixes. /watch? is for videos, /user/ is for user accounts
			// Note that CURL displays /user/ as \/user\/, so the extra "\"" characters must be removed
			// Both url types end with a ", hence it is used as the suffix
			std::string urlStart[] = {"/watch?", "\\/user\\/"};
			std::string urlEnd = "\"";

			// The outside loop iterates through all data received from talk_to_multiple_websites
			// The inside loop does the actual searching through each portion of the recv_buffer
			for (int i = 0; i < recv_buffer.size(); ++i){
				while (true){
					
					// The raw urls extracted from the data will not have the youtube.com prefix, so we add it first
					temp_url.assign(url_prefix);
					
					// startPos and endPos determine the boundaries of a url
					int startPos = recv_buffer[i].find(urlStart[iter]);
					if (startPos == std::string::npos){
						break;
					}
					int endPos = recv_buffer[i].find_first_of(urlEnd, startPos);

					// Extract the url and remove it from the recv_buffer[i]
					temp_url.append(recv_buffer[i].substr(startPos, endPos - startPos));
					recv_buffer[i].erase(startPos, endPos - startPos);

					// Make sure we are not repeating any urls already in the text file,
					// as well as urls previously extracted from recv_buffer
					if (url_lookup.find(temp_url) != url_lookup.end() ||
						temp_url_lookup.find(temp_url) != temp_url_lookup.end()){
						continue;
					}

					urllist.push_back(temp_url);

					// The original url_lookup hash table will be updated in save_urls_to_file(),
					// when the "urllist" is ready to be added to file. Hence, we only need to update
					// the second hash table (temp_url_lookup) here
					temp_url_lookup.insert(temp_url);
				}
			}
			recv_buffer.clear();

			// no need to clear temp_url_lookup at it will be "destroyed" after exiting this function
		}

		/**************************************************************************************
		
		Main function to run the web crawler and write to the text file

		**************************************************************************************/
		void manage_web_crawler(){
			
			int iter;

			filehandler.open("urls.txt", std::ofstream::out);
			if (filehandler.is_open()){

				// Crawler will stop after 2 iterations, as mentioned at the beginning of this file
				for (iter = 0; iter < MAX_ITER; ++iter){
					std::cout << "Iteration " << iter << std::endl;
					talk_to_multiple_websites();
					save_urls_to_file(iter);
					parse_through_acquired_data(iter);
					sleep(5);
				}
				
				// One extra iteration to save the urls from the last loop of parse_through_acquired_data()
				save_urls_to_file(iter);
				filehandler.close();
			}
			else{
				std::cout << "Error opening file" << std::endl;
			}

		}

};

int main(int argc, char const *argv[]){

	// Fill in any initial urls in this list
	std::string initial_url = "https://www.youtube.com/user/NUScast/videos";
	
	curl_global_init(CURL_GLOBAL_ALL);
	Crawler test(initial_url);
	test.manage_web_crawler();
	curl_global_cleanup();

	return 0;
}
