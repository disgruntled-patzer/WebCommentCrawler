# Web Comment Crawler

Web Crawler application to parse through an online Youtube channel, find out the list of related users, and rank them in terms of appearance frequency

## Instructions on how to run

Open the file webcrawler.py and set the parameters defined as follows:

* **THREAD_NUM** = Number of parallel threads you wish to use in this program
* **TIME_TO_RUN** = Time for the webcrawler to run in seconds
* **ORIGINAL_CHAN** = User name of the Youtube channel you wish to crawl

After that, just run the command `python3 webcrawler.py`. After **TIME_TO_RUN** seconds has passed, the list of related users will be displayed in `user.txt`, sorted in appearance frequency.
