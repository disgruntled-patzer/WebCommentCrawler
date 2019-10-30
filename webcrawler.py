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
USER_FILE = "user.txt" # txt file containing links of user profiles
THREAD_NUM = 8 # number of threads used in this program
TIME_TO_RUN = 120 # time for the webcrawler to run in seconds
ORIGINAL_CHAN = "NUScast" # Original youtube channel user name

start_time = time() # the time program is executed
queued_set = set() # set of links to be crawled
crawled_set = set() # set of links already crawled
user_map = {} # dictionary (hash map) of user profile links to appearance count
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
                print("Unable to add \"" + item + "\" to file. " + str(e))

# Method to load content of dictionary into file
def dictionary_to_file(content, file):
    with open(file, encoding='UTF-8', mode='w') as f:
        try:
            # sort list of users in alphabetical order
            sorted_list = sorted(content.items(), key=lambda kv: kv[0].lower())
            # sort list in descending order of appearances
            sorted_list = sorted(sorted_list, key=lambda kv: kv[1], reverse=True)
            for kv in sorted_list:
                f.write(kv[0] + ' (' + str(kv[1]) + ')\n')
        except Exception as e:
            if __debug__:
                print("Unable to add \"" + str(kv) + "\" to file. " + str(e))

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
        self.users = set()
        self.valid_link_ids = ["/watch?", "/user/"]

    def handle_starttag(self, tag, attrs):
        # finds for anchor tags in page
        if tag == "a":
            # gets value of href
            for attribute, value in attrs:
                if attribute == "href":
                    # ensures that link added is a full link
                    link_fragments = urlsplit(self.page_url)
                    link = urljoin(link_fragments[0] + "://" + link_fragments[1], value)
                    link_type = self.verify_links(link)
                    # add link to set for crawling
                    if (link_type == 1):
                        self.links.add(link)
                    # add link to set for tracking user count
                    elif (link_type == 2):
                        self.users.add(link)
                        

    # Only accept "valid" links
    def verify_links(self, link):
        # We don't want to revisit any links on the original channel
        if ORIGINAL_CHAN in link:
            return False
        if self.valid_link_ids[0] in link:
            return 1 # video watch links
        elif self.valid_link_ids[1] in link:
            return 2 # user profile links
        return False

    def get_links(self):
        return [self.links, self.users]

# Method to retrieve links in page
def get_webpage_links(link):
    elapsed_time = -1
    parser = WebpageParser(link)
    try:
        # set timeout on request call to be 15s
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

# Method to add retrieved video links to the queued set
def add_links_to_queue(links):
    for link in links:
        if link not in queued_set and link not in crawled_set:
            queued_set.add(link)

# Method to add retrieved user profile links to user list
def add_users(links):
    for link in links:
        parsed_link = link.split("?")
        # check if user link is to profile and not subscribe button
        if len(parsed_link) == 1:
            # get name of user
            user = parsed_link[0].split("/")[-1]
            if user not in user_map:
                user_map[user] = 0
            user_map[user] += 1

# Method to update contents of the users, queued and crawled files
def update_files(crawled_link, elapsed_time):
    dictionary_to_file(user_map, USER_FILE)
    set_to_file(queued_set, QUEUED_FILE)
    if elapsed_time != -1:
        content = crawled_link + " (" + str(elapsed_time) + "ms)"
        append_to_file(content, CRAWLED_FILE)

# Method to start crawling a page and update queued and crawled links from the result
def crawl_page(link):
    # checks that no duplicate page crawling occur
    if link not in crawled_set:
        elapsed_time, links = get_webpage_links(link)
        video_links = links[0]
        user_links = links[1]
        try:
            queued_set.remove(link)
            crawled_set.add(link)
            add_users(user_links)
            add_links_to_queue(video_links)
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
    initial_link = ("https://www.youtube.com/user/" + ORIGINAL_CHAN + "/videos")
    queued_set.add(initial_link)
    crawl_page(list(queued_set)[0]) # crawl the first link in the file
    create_workers()
    update_jobs()

start() # runs the webcrawler program
