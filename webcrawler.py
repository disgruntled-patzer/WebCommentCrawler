import os
from html.parser import HTMLParser
from queue import Queue
from threading import Thread
from threading import current_thread
from time import sleep
from time import time
from urllib.request import urlopen
from urllib.parse import urljoin
from urllib.parse import urlsplit

QUEUED_FILE = "queued.txt" # txt file containing links to be crawled
CRAWLED_FILE = "crawled.txt" # txt file containing links already crawled
THREAD_NUM = 8 # number of threads used in this program
TIME_TO_RUN = 10 # time for the webcrawler to run in seconds

start_time = time() # the time program is executed
queued_set = set() # set of links to be crawled
crawled_set = set() # set of links already crawled
queue = Queue() # queue of links to be crawled (used for worker threads)

#####################################################
#                                                   #
#   Helper methods for file reading and writing     #
#                                                   #
#####################################################

# Method to append a line to file
def append_to_file(content, file):
    with open(file, encoding='UTF-8', mode='a') as f:
        # newline to make file human readable
        f.write(content + '\n')

# Method to create/overwrite file
def write_to_file(content, file):
    with open(file, encoding='UTF-8', mode='w') as f:
        f.write(content)

# Method to load content of file into set
def file_to_set(file):
    with open(file, encoding='UTF-8', mode='r') as f:
        return set(line.strip() for line in f)

# Method to load content of set into file
def set_to_file(set_content, file):
    with open(file, encoding='UTF-8', mode='w') as f:
        try:
            for item in set_content:
                f.write(item + '\n')
        except Exception as e:
            if __debug__:
                print("Unable to add \"" + item + "\" to file." + str(e))
            

#####################################################
#                                                   #
#   Crawling and parsing HTML web pages from link   #
#                                                   #
#####################################################

# HTML parser class to retrieve links in page
class WebpageParser(HTMLParser):
    def __init__(self, page_url):
        super().__init__()
        self.page_url = page_url
        self.links = set()

    def handle_starttag(self, tag, attrs):
        # finds for anchor tags in page
        if tag == "a":
            # gets value of href
            for attribute, value in attrs:
                if attribute == "href":
                    # ensures that link added is a full link
                    link_fragments = urlsplit(self.page_url)
                    link = urljoin(link_fragments[0] + "://" + link_fragments[1], value)
                    self.links.add(link)

    def get_links(self):
        return self.links
    
# Method to retrieve links in page
def get_webpage_links(link):
    elapsed_time = -1
    parser = WebpageParser(link)
    try:
        # set timeout on rqeuest call to be 15s
        response = urlopen(link, timeout=15)        
        start = time()
        # check that the link is a valid html page
        if 'text/html' in response.getheader('Content-Type'):
            webpage = response.read().decode('utf-8')
            end = time()
            # time taken to send request and process response in ms
            elapsed_time = int((end - start) * 1000)
            # extract links from page
            parser.feed(webpage)
    except Exception as e:
        if __debug__:
            print("Unable to retrieve \"" + link + "\". " + str(e))
    return elapsed_time, parser.get_links()

# Method to add retrieved links to the queued set
def add_links_to_queue(links):
    for link in links:
        if link not in queued_set and link not in crawled_set:
            queued_set.add(link)

# Method to update contents of the queued and crawled files
def update_files(crawled_link, elapsed_time):
    set_to_file(queued_set, QUEUED_FILE)
    if elapsed_time != -1:
        content = crawled_link + " (" + str(elapsed_time) + "ms)"
        append_to_file(content, CRAWLED_FILE)

# Method to start crawling a page and update queued and crawled links from the result
def crawl_page(link):
    # checks that no duplicate page crawling occurs
    if link not in crawled_set:
        elapsed_time, links = get_webpage_links(link)
        try:
            queued_set.remove(link)
            crawled_set.add(link)
            add_links_to_queue(links)
        except Exception as e:
            if __debug__:
                print(e)
        update_files(link, elapsed_time)

#####################################################
#                                                   #
#   Methods for doing multithreaded webcrawling     #
#                                                   #
#####################################################

# Method to initialize worker threads
def create_workers():
    for x in range(THREAD_NUM):
        t = Thread(target=work)
        t.daemon = True # ensures threads are killed if program exits
        t.start()

# Worker thread retrieves a link to crawl and ensures synchronization
def work():
    while True:
        elapsed_run_time = time() - start_time
        if elapsed_run_time > TIME_TO_RUN:
            print("Set time passed. Crawling completed. Crawled: ", len(crawled_set))
            os._exit(1)
        link = queue.get()
        if __debug__:
            print(current_thread().name + " crawling " + link)
        crawl_page(link)
        queue.task_done()
        sleep(1)

# Refreshes jobs in queue everytime it is exhausted
def update_jobs():
    while True:
        elapsed_run_time = time() - start_time
        queued_set = file_to_set(QUEUED_FILE)
        if len(queued_set) != 0:
            if __debug__:
                print(str(len(queued_set)) + " links queued")
                print(str(len(crawled_set)) + " links crawled")
            for link in queued_set:
                queue.put(link)
                queue.join() # waits for current list to be exhausted before next update
        else:
            print("No more valid links to crawl")
            break
        
# Method to kickstart webcrawler
def start():
    global queued_set
    queued_set = file_to_set(QUEUED_FILE)
    crawl_page(list(queued_set)[0]) # crawl the first link in the file
    create_workers()
    update_jobs()

start() # runs the webcrawler program
