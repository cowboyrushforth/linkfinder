// Linkfinder v0.0.1
//
// This is a zeromq server designed
// to run a multi-threaded service
// that fetches metadata about a given
// link and returns it to a requestor.

// right now it literally returns links in
// the url's html, but will eventually
// return interesting data about given urls
// especially things like title, description, thumbnails, etc

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLtree.h>
#include <stdbool.h>
#include "lib/zhelpers.h"
#include <json/json.h>

// how many worker threads?
#define NUM_WORKERS 4

// struct to hold results from curl
struct ResultBuffer {
  unsigned char *memory;
  size_t size;
  char **links;
  int links_found;
};

void FindLinks(htmlNodePtr element, struct ResultBuffer* buffer) {

  htmlNodePtr node;
  for(node = element; node != NULL; node = node->next) {

      if(node->type == XML_ELEMENT_NODE) {

        if(xmlStrcasecmp(node->name, (const xmlChar*)"A") == 0) {

          xmlAttrPtr attr;
          for(attr = node->properties; attr != NULL; attr = attr->next) {
            if(xmlStrcasecmp(attr->name, (const xmlChar*)"HREF") == 0) {
              // realloc buffer links
              buffer->links = (char**)realloc(buffer->links, (buffer->links_found+1) * sizeof(char*));

              // malloc new string
              buffer->links[buffer->links_found] = malloc( sizeof(char) * (strlen((char*)attr->children->content) + 1));

              // copy xml data into string
              strcpy(buffer->links[buffer->links_found], (char*)attr->children->content);

              // increment for next time
              buffer->links_found++;
            }
          }
      }
      if(node->children != NULL)
      {
        FindLinks(node->children, buffer);
      }
    }
  }

}

void ParseHTML(struct ResultBuffer* buffer) {

  printf("--> About to Parse...\n");


  // xmlChar *xstr = malloc(sizeof(html));
  // xstr = xmlCharStrdup(html);
  htmlDocPtr doc = htmlReadDoc(buffer->memory, NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOWARNING | HTML_PARSE_NOBLANKS);

  printf("--> Parsed...\n");

  if(doc != NULL) {
    printf("--> At HTML ROOT...\n");
    htmlNodePtr root = xmlDocGetRootElement(doc);

    printf("--> FINDING LINKS...\n");
    if(root != NULL) {
      FindLinks(root, buffer);
    }
    printf("--> DONE FINDING LINKS!...\n");
    printf("--> %d Links Found!...\n", buffer->links_found);
    int x;
    for(x = 0; x < buffer->links_found; x++) {
      printf("link: %s\n", buffer->links[x]);
    }

    xmlFreeDoc(doc);
    doc = NULL;
  }
  //free(xstr);
}


static size_t WriteResultBuffer(void *ptr, size_t size, size_t nmemb, void *data) {
  size_t realsize = size * nmemb;
  struct ResultBuffer *mem = (struct ResultBuffer *)data;

  mem->memory = realloc(mem->memory, mem->size + realsize + 1);
  if (mem->memory == NULL) {
    /* out of memory! */ 
    printf("not enough memory (realloc returned NULL)\n");
    exit(EXIT_FAILURE);
  }

  memcpy(&(mem->memory[mem->size]), ptr, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

json_object * craftReply(struct ResultBuffer* buffer) {


  /*  more examples:
      json_object *jint = json_object_new_int(10);
      json_object *jboolean = json_object_new_boolean(1);
      json_object *jdouble = json_object_new_double(2.14);
      json_object_object_add(jobj,"mykey", jarray);
      */
  // setup json object
  json_object * jobj = json_object_new_object();
  // setup links array
  json_object *links_array = json_object_new_array();

  // append links to array
  int y;
  for(y = 0; y < buffer->links_found; y++) {
    json_object *str = json_object_new_string(buffer->links[y]);
    json_object_array_add(links_array,str);
  }

  // append links array to object
  json_object_object_add(jobj, "links", links_array);

  return jobj;
}


static void * worker_routine (void *context) {

  // Setup Socket to talk to dispatcher
  void *receiver = zmq_socket (context, ZMQ_REP);
  zmq_connect (receiver, "inproc://workers");

  s_catch_signals();


  while (1) { 
    // get request string fro 0mq
    char *raw_url = s_recv (receiver);  // receive raw url

    // fire up curl
    CURL *curl;      // curl context
    CURLcode code;   // curl http return code

    // setup buffer
    struct ResultBuffer buffer;
    buffer.memory = malloc(1);  
    buffer.links = malloc(1);
    buffer.size = 0; 
    buffer.links_found = 0;

    // handle signals
    if(s_interrupted) {
      printf("Interrupted\n");
      break;
    } else {
      printf ("\n\nReceived request for raw_url: [%s]\n", raw_url);
    }

    // Setup CURL
    curl = curl_easy_init();
    if (curl == NULL) {
      fprintf(stderr, "Failed to create CURL connection\n");
      goto bad_message;
    } 
    code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    if(code != CURLE_OK) {
      fprintf(stderr, "failed to set no signal\n");
      goto bad_message;
    }
    code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    if (code != CURLE_OK) {
      fprintf(stderr, "Failed to set redirect option\n");
      goto bad_message;
    }
    curl_easy_setopt(curl,  CURLOPT_WRITEFUNCTION, WriteResultBuffer);
    if (code != CURLE_OK) {
      fprintf(stderr, "Failed to set writer\n");
      goto bad_message;
    }
    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &buffer);
    if (code != CURLE_OK) {
      fprintf(stderr, "Failed to set write data\n");
      goto bad_message;
    }
    // oh yeah, this is totally firefox!
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.2b5) Gecko/20091204 Firefox/3.6b5");

    printf("--> Fetching\n");

    //for now just assume that this is a valid url and fetch
    //its contents to memory
    //TODO: we need to limit how much we can put in of course.
    //      no downloading of huge stuff
    curl_easy_setopt(curl, CURLOPT_URL, raw_url);
    code = curl_easy_perform(curl);

    // if we got a 500, 404, network error, etc, bail
    if (code != CURLE_OK)  {
      fprintf(stderr, "Failed to get '%s'\n", raw_url);
      fprintf(stderr, "code was: %d \n", code);
      goto bad_message;

      // else parse it!
    } else {
      printf("--> Parsing... \n");
      ParseHTML(&buffer);
      printf("---> DONE PARSING...\n");
    }


    // get result back to client
    json_object *jobj;
    // fetch json obj
    jobj = craftReply(&buffer); 
    // send to client
    s_send (receiver, (char*) json_object_to_json_string(jobj));

    // cleanup
    if(raw_url) {
      free(raw_url);
    }
    if(buffer.links) {
      int k;
      for(k = 0; k < buffer.links_found; k++) {
        free(buffer.links[k]);
      }
      free(buffer.links);
    }
    if(buffer.memory) {
      free(buffer.memory);
    }
    curl_easy_cleanup(curl);
    json_object_put(jobj);

    continue;

bad_message:
    printf("BAD MESSAGE!");
    // cleanup
    if(buffer.memory) {
      free(buffer.memory);
    }
    if(raw_url) {
      free(raw_url);
    }
    curl_easy_cleanup(curl);

    s_send (receiver, "ERROR");
    if(s_interrupted) {
      break;
    }
  }


  printf("DONE\n");
  s_send(receiver, "DONE");
  zmq_close (receiver);
  return NULL;
}



int main (void) {

  s_version_assert (2, 1);
  void *context = zmq_init (1);

  // init LibXML
  xmlInitParser();

  // Tcp Socket to talk to clients
  void *clients = zmq_socket (context, ZMQ_XREP);
  zmq_bind (clients, "tcp://*:5555");

  // Internal Socket to talk to workers
  void *workers = zmq_socket (context, ZMQ_XREQ);
  zmq_bind (workers, "inproc://workers");

  // Launch pool of worker threads
  int thread_nbr;
  for (thread_nbr = 0; thread_nbr < NUM_WORKERS; thread_nbr++) {
    pthread_t worker;
    pthread_create (&worker, NULL, worker_routine, context);
  }

  // Connect work threads to client threads via a queue
  printf("starting device\n");
  zmq_device(ZMQ_QUEUE, clients, workers);

  // We never get here but clean up anyhow
  zmq_close(clients);
  zmq_close(workers);
  zmq_term(context);
  return 0;
}
