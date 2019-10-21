/*
CS3103 Assignment 1 Part C
Lau Yan Han, A0164750E
Sources:
https://github.com/jarvis57/Web-Crawler
https://github.com/MelvinKool/WebCrawler
https://curl.haxx.se/libcurl/c/example.html
https://stackoverflow.com/questions/9786150/save-curl-content-result-into-a-string-in-c

This parallel webcrawler implementation acts on a pre-specified list (vector) of urls, parses through them
for additional urls, and repeated the process for a specified number of iterations

Brief overview:

	- The main function initiates the initial url list and curl, and calls the Web Crawler class main function
	- The Web Crawler main function (Crawler::manage_web_crawler) opens the main text file where urls will be written to
	- Web Crawler main function initiates the following:
		1. Talk to the websites in the url list and retrieve data from them
		2. Save the urls into the text file, and clear the url list for later use
		3. Parse through the retrieved data, extract urls, verify them, and add them to the (same) url list
		4. Repeat this step for a specified number of iterations, before closing the file
 */

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

// Min response time for a given url in seconds. Responding too quickly means the URL is not valid (hence no data to transfer)
// Based on test runs, invalid websites give a response time ~ 5e-5 seconds (0.005ms), hence set the threshold slightly higher
#define MIN_RESPONSE_TIME 1e-4

// Max number of iterations that the web crawler will run the main loop for. Note that due to the implementation
// of the crawler, there will be one extra iteration after exiting the loop (hence total of MAX_ITER + 1)
#define MAX_ITER 4

/* Takes in a url (extracted from web page) and verify whether it is according to our specified standards */
class url_verifier{
	private:
		std::string url;

		// Verify that the url does not point to a pdf/jpg etc... file of any sort (we can still accept html + xml)
		bool check_for_file_urls() {
			std::string blacklist[] = {".css", ".js", ".pdf", ".png", ".jpeg", 
				".jpg", ".gif", ".ico", "docx", "xlsx", "mailto:"};						
			bool flag = false;
			for (auto it : blacklist) 
				if (url.find(it) != std::string::npos){
					flag = true;
				}
			return flag;
		}
		
		// Verify that the url belongs to a whitelisted domain
		bool check_domain() {
			std::string whitelist[] = {".com", ".sg", ".net", ".co", ".org", ".me", ".load"};		
			bool flag = false;
			for (auto it : whitelist) 
				if (url.find(it) != std::string::npos) {
					flag = true;									
				}														
			return flag;
		}
	
	public:

		url_verifier(std::string urlinput){
			url.assign(urlinput);
		}

		/*
		Our requirements for an "acceptable url" are:
		- It must belong to a whitelisted domain of our choice (.com, .sg, etc). This may lead to some valid urls being rejected
		- It should not point to any online file (e.g. pdf) or SMTP mail server, as that will make it very difficult to parse
		*/
		bool verifyURL(){

			bool flag = true;
			if (!check_domain()){
				flag = false;
			}
			if (check_for_file_urls()){
				flag = false;
			}
			if (flag){
				std::cout << url.c_str() << " accepted" << std::endl;
			}
			else{
				std::cout << url.c_str() << " rejected" << std::endl;
			}
			return flag;
		}

};

/* Main class to handle web crawler */
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

		/* Constructor to fill up the initial list of urls */
		Crawler(std::vector<std::string>&initial_urls){
			for (int i = 0; i < initial_urls.size(); ++i){
				urllist.push_back(initial_urls[i]);
			}
		}

		/* Run the CURL multi handler to receive data from websites mentioned in the urllist vector */
		void talk_to_multiple_websites(){
			
			if (urllist.empty()){
				return;
			}
			
			// Variables for curl multi-handling
			CURLM *multi_handle;
			std::vector<CURL*> url_handles;
			int still_running = 0;

			// Temporary holding variables to fill the url_handles and recv_buffer vectors
			CURL *temp_handle;
			std::string temp_recv_buffer;
			int i;

			// The length of the url list is previously determined
			int urllist_len = urllist.size();

			multi_handle = curl_multi_init();

			// For some reason, performing recv_buffer.push_back together with all the operations in the next loop
			// results in a memory overflow (possible because the recv_buffer is holding lots of data);
			// breaking this operation out into a separate loop solves the problem
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
				int rc, maxfd = -1; // rc = select() return code, maxfd = represents how many file descriptors are ready
				CURLMcode mc; // curl_multi_fdset() return code
				
				fd_set fdread, fdwrite, fdexcep;

				FD_ZERO(&fdread);
				FD_ZERO(&fdwrite);
				FD_ZERO(&fdexcep);

				// Here we set the timeout value to 1 sec
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
				// (the minimum suggested value suggested in curl_multi_fdset doc)
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
						// Select Error
						break;
					case 0:
					default:
						curl_multi_perform(multi_handle, &still_running);
						break;
				}
			}

			// Print the received data + response time
			// To-do: Call curl_multi_info_read to check if any urls return invalid responses
			for (i = 0; i < urllist_len; ++i){
				url_response_time.push_back(-1);
				curl_easy_getinfo(url_handles[i], CURLINFO_TOTAL_TIME, &url_response_time[i]);
			}

			// Cleanup
			curl_multi_cleanup(multi_handle);
			for (i = 0; i < urllist_len; ++i){
				curl_easy_cleanup(url_handles[i]);
			}

		}

		/*
		Save the urls in urllist to the text file and the url_lookup hash table, then clear the urllist.
		This function should be called immediately after curl multi handler talks to the urls, so that
		the urllist can be cleared to accomodate new incoming urls when parsing through acquired data
		*/
		void save_urls_to_file(){
			
			if (urllist.empty()){
				return;
			}
			std::stringstream ss;
			for (int i = 0; i < urllist.size(); ++i){
				
				// If response time is too small, it means the link doesn't exist (hence no effort was made to access its data)
				if (url_response_time[i] < MIN_RESPONSE_TIME){
					continue;
				}

				// A stringstream is used to compile all relevant info and send it to the filehandler
				// The url is inserted into the lookup table, and the stringstream is cleared for the next iteration
				ss << urllist[i].c_str() << ", " << 100 * url_response_time[i] << " ms\n";
				
				filehandler << ss.str();
				url_lookup.insert(urllist[i]); // Save the url for quick (O(1) time) lookup later
				ss.str(std::string());
			}
			urllist.clear();
			url_response_time.clear();
		}

		/*
		Parse through each data in the recv_buffer using parser tools
		Then save all detected urls into the urllist (to be handled by the curl multi handler later)
		Any duplicate urls, and urls previously in the text file will be ignored; this is done with the help of the
		url_lookup and temp_url_lookup hash tables, which check for duplicates in O(1) time (at the cost of extra memory)
		Credits to https://github.com/jarvis57/Web-Crawler for the parser code, which we have inherited here
		*/
		void parse_through_acquired_data(){
			
			std::string temp_url;

			// A second lookup table to temporarily store urls that are extracted immediately from the acquired data
			// This second table is neccessary to make sure any duplicates WITHIN the acquired data are eliminated
			// before the next iteration of accessing these urls. Relying solely on the original url_lookup table will not
			// work, since the original url_lookup table only includes urls from PREVIOUS iterations
			std::unordered_set<std::string> temp_url_lookup;

			// URL's possible prefixes and suffixes. We only accept http, https and www; ignore href urls
			// For suffixes, we include #, ? for not counting the hash & query, and also the case where the url is enclosed in brackets
			std::string urlStart[] = {"http://", "https://", "www."};
			std::string urlEnd = "\"?#, )'<>";

			// The outside loop iterates through all data received from talk_to_multiple_websites
			// The middle loop iterates through urlStart to search for urls beginning with each prefix
			// The inside loop does the actual searching through each portion of the recv_buffer
			for (int i = 0; i < recv_buffer.size(); ++i){
				for (auto start_iter : urlStart){
					while (true){
						
						// startPos and endPos determine the boundaries of a url
						int startPos = recv_buffer[i].find(start_iter);
						if (startPos == std::string::npos){
							break;
						}
						int endPos = recv_buffer[i].find_first_of(urlEnd, startPos);

						// Extract the url and remove it from the recv_buffer[i]
						temp_url = recv_buffer[i].substr(startPos, endPos - startPos);
						recv_buffer[i].erase(startPos, endPos - startPos);

						// Make sure we are not repeating any urls already in the text file,
						// as well as urls previously extracted from recv_buffer
						if (url_lookup.find(temp_url) != url_lookup.end() ||
							temp_url_lookup.find(temp_url) != temp_url_lookup.end()){
							continue;
						}

						// Verify that the url is in accordance with our requirements
						url_verifier temp_url_verifier(temp_url);

						if(temp_url_verifier.verifyURL()){
							
							urllist.push_back(temp_url);
							
							// The original url_lookup hash table will be updated in save_urls_to_file(),
							// when the "urllist" is ready to be added to file
							temp_url_lookup.insert(temp_url);
						}

						temp_url.clear();
					}
				}
			}
			recv_buffer.clear();
			// no need to clear temp_url_lookup at it will be "destroyed" after exiting this function
		}

		/* Main function to run the web crawler and write to the text file */
		void manage_web_crawler(){
			
			filehandler.open("urls.txt", std::ofstream::out);
			if (filehandler.is_open()){

				// Crawler will stop after 4 iterations. From prior testing, the number of urls accessed
				// grows exponentially after each iteration, and becomes unacceptably slow after 4-5 iterations
				// After each iteration, sleep for 5 seconds (prevents webpage flooding, which can be misinterpreted as DOS attack)
				for (int i = 1; i <= MAX_ITER; ++i){
					std::cout << "Iteration " << i << std::endl;
					talk_to_multiple_websites();
					save_urls_to_file();
					parse_through_acquired_data();
					sleep(5);
				}
				
				// One extra iteration to save the urls + their response times from the last loop of parse_through_acquired_data()
				talk_to_multiple_websites();
				save_urls_to_file();
				filehandler.close();
			}
			else{
				std::cout << "Error opening file" << std::endl;
			}

		}

};

int main(int argc, char const *argv[]){

	// Fill in any initial urls in this list
	std::vector<std::string>initial_urls = {
		"https://www.google.com/",
		"https://www.youtube.com/",
		"https://www.example.com/"
	};
	
	curl_global_init(CURL_GLOBAL_ALL);
	Crawler test(initial_urls);
	test.manage_web_crawler();
	curl_global_cleanup();

	return 0;
}
