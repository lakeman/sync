
#include <signal.h>
// include the whole sync implementation so we can screw around with its internals
#include "sync.c"
#include "log.h"

struct test_peer;

struct test_key{
  sync_key_t key;
  struct test_peer *initial_peer;
};

struct test_peer{
  char name[10];
  struct sync_state *state;
};

struct test_transfer{
  struct test_transfer *next;
  struct test_peer *src;
  struct test_peer *dest;
  struct test_key *key;
  unsigned delay_till;
};

struct test_transfer *transfer_head=NULL, *transfer_tail=NULL;
unsigned quit = 0;
unsigned packets_sent = 0;

// transmit one message from peer_index to all other peers
static int send_data(struct test_peer *peers, unsigned peer_count, unsigned peer_index)
{
  int ret = 0;
  
  uint8_t packet_buff[200];
  LOGF("Sending packet from %s", peers[peer_index].name);
  size_t len = sync_build_message(peers[peer_index].state, packet_buff, sizeof(packet_buff));
  for (unsigned i=0;i<peer_count;i++){
    if (i!=peer_index){
      LOGF("Receiving by %s", peers[i].name);
      int r = sync_recv_message(peers[i].state, &peers[peer_index], packet_buff, len);
      assert(r==0);
      // does peer peer_index know about all the actual differences between itself and peer i
      
      key_message_t message;
      if (peers[peer_index].state->root){
	message = peers[peer_index].state->root->message;
      }else{
	bzero(&message, sizeof message);
	message.stored = 1;
      }
      
      struct sync_peer_state *peer_state = peers[i].state->peers;
      assert(peer_state);
      while(peer_state->peer_context != &peers[peer_index]){
	peer_state = peer_state->next;
	assert(peer_state);
      }
      
      remove_differences(peer_state, &message);
      
      if (peers[i].state->root){
	if (cmp_message(&message, &peers[i].state->root->message)!=0)
	  ret = 1;
      }else{
	unsigned k;
	for (k=0;k<KEY_LEN;k++)
	  if (message.key.key[k])
	    break;
	if (k==KEY_LEN)
	  ret = 1;
      }
    }
  }
  return ret;
}

void test_peer_has (void *context, void *peer_context, const sync_key_t *key){
  struct test_peer *state = (struct test_peer *)context;
  struct test_peer *peer = (struct test_peer *)peer_context;
  
  LOGF("%s - %s has %s that we need", 
    state->name, peer->name, alloca_sync_key(key));
    
  assert(sync_key_exists(peer->state, key)==1);
  assert(sync_key_exists(state->state, key)==0);
}

void test_peer_does_not_have (void *context, void *peer_context, void *key_context, const sync_key_t *key){
  struct test_peer *state = (struct test_peer *)context;
  struct test_peer *peer = (struct test_peer *)peer_context;
  struct test_key *test_key = (struct test_key *)key_context;
  
  LOGF("%s - %s does not have %s that we need to send", 
    state->name, peer->name, alloca_sync_key(key));
  
  assert(sync_key_exists(state->state, key)==1);
  assert(sync_key_exists(peer->state, key)==0);
  assert(test_key->initial_peer);
  
  struct test_transfer *transfer = allocate(sizeof(struct test_transfer));
  transfer->src = state;
  transfer->dest = peer;
  transfer->key = test_key;
  transfer->delay_till = packets_sent + 10;
  if (transfer_tail){
    transfer_tail->next = transfer;
  }else{
    transfer_head = transfer;
  }
  transfer_tail = transfer;
}

void test_peer_now_has (void *context, void *peer_context, void *key_context, const sync_key_t *key){
  struct test_peer *state = (struct test_peer *)context;
  struct test_peer *peer = (struct test_peer *)peer_context;
  
  LOGF("%s - %s has now received %s", 
    state->name, peer->name, alloca_sync_key(key));
    
  assert(sync_key_exists(state->state, key)==1);
  assert(sync_key_exists(peer->state, key)==1);
}

static void signal_handler(int signal)
{
  if (quit)
    exit(-1);
  LOG("Shutting down test");
  quit = 1;
}

// test this synchronisation protocol by;
// generating sets of keys
// swapping messages
// stopping when all nodes agree on the set of keys
// logging packet statistics
int main(int argc, char **argv)
{
  
  /* Catch SIGHUP etc so that we can respond to requests to do things, eg, shut down. */
  struct sigaction sig;
  bzero(&sig, sizeof sig);
  
  sig.sa_handler = signal_handler;
  sigemptyset(&sig.sa_mask);
  sig.sa_flags = 0;
  
  sigaction(SIGINT, &sig, NULL);
    
  // setup peer state
  unsigned peer_count=argc > 2 ? argc-2 : 2;
  
  // a sync state per peer
  struct test_peer peers[peer_count];
  unsigned unique[peer_count];
  
  unsigned common=100;
  if (argc>=2)
    common = atoi(argv[1]);
    
  unsigned i, j, total_keys=common;
  for (i=0;i<peer_count;i++){
    snprintf(peers[i].name, 10, "Peer %u", i);
    peers[i].state = sync_alloc_state(&peers[i], test_peer_has, test_peer_does_not_have, test_peer_now_has);
    
    unique[i]=10;
    if (argc>i+2)
      unique[i] = atoi(argv[i+2]);
    total_keys+=unique[i];
  }
  struct test_key test_keys[total_keys];
  bzero(test_keys, sizeof test_keys);
  
  unsigned key_index=0;
  
  LOG("--- Adding keys ---");
  {
    int fdRand = open("/dev/urandom",O_RDONLY);
    assert(fdRand!=-1);
    
    LOGF("Generating %u common key(s)", common);
    for (i=0; i<common; i++){
      assert(read(fdRand, test_keys[key_index].key.key, KEY_LEN)==KEY_LEN);
      for (j=0;j<peer_count;j++)
	sync_add_key(peers[j].state, &test_keys[key_index].key, &test_keys[key_index]);
      key_index++;
    }
    
    for (i=0; i<peer_count; i++){
      LOGF("Generating %u unique key(s) for %s", unique[i], peers[i].name);
      
      for (j=0;j<unique[i];j++){
	test_keys[key_index].initial_peer = &peers[i];
	assert(read(fdRand, test_keys[key_index].key.key, KEY_LEN)==KEY_LEN);
	sync_add_key(peers[i].state, &test_keys[key_index].key, &test_keys[key_index]);
	key_index++;
      }
    }
    close(fdRand);
    assert(key_index == total_keys);
  }
  
  // debug, dump the tree structure
  LOG("--- BEFORE ---");
  for (i=0; i<peer_count; i++){
    LOGF("%s - %d Keys known", 
      peers[i].name,
      peers[i].state->key_count);
    //dump_tree(&peer_left.root,0);
  }
  
//sync_again:
  LOG("--- SYNCING ---");
  // send messages to discover missing keys
  uint8_t trees_differ = 1;
  
  // TODO quick test for no progress?
  while(quit==0 && (trees_differ>0 || transfer_head)){
    trees_differ = 0;
    for (i=0;i<peer_count;i++){
      
      // stop if this peer has sent lots of packets and not made any progress
      if (peers[i].state->progress>50){
	LOGF("%s - Quitting after no progress for %u packets", peers[i].name, peers[i].state->progress);
	quit=1;
	break;
      }
	
      // transmit one message from peer i to all other peers
      if (send_data(peers, peer_count, i)>0)
	trees_differ = 1;
      packets_sent++;
      
      // transfer during sync!
      while(transfer_head && transfer_head->delay_till <= packets_sent){
	struct test_transfer *transfer = transfer_head;
	transfer_head = transfer->next;
	if (!transfer_head)
	  transfer_tail = NULL;
	
	LOGF("%s - %s, *** Faking transfer complete for %s", 
	  transfer->src->name, transfer->dest->name, alloca_sync_key(&transfer->key->key));
	
	sync_add_key(transfer->dest->state, &transfer->key->key, transfer->key);
	
	free(transfer);
      }
    }
  }
  
  if (!quit){
    LOG("--- SYNCING COMPLETE ---");
    LOGF("Sync has identified all missing keys after %u packets", packets_sent);
  }
  
  for (i=0;i<peer_count;i++){
    LOGF("%s - Keys %u, sent %u, sent root %u, messages %u, received %u, uninteresting %u", 
      peers[i].name, 
      peers[i].state->key_count, 
      peers[i].state->sent_messages,
      peers[i].state->sent_root,
      peers[i].state->sent_record_count, 
      peers[i].state->received_record_count,
      peers[i].state->received_uninteresting);
    
      struct sync_peer_state *peer_state = peers[i].state->peers;
      while(peer_state){
	struct test_peer *peer = (struct test_peer *)peer_state->peer_context;
	
	LOGF("%s - believes that %s, needs %u key(s) & has %u key(s) we need",
	  peers[i].name,
	  peer->name,
	  peer_state->send_count,
	  peer_state->recv_count
	);
	
	peer_state = peer_state->next;
      }
  }
  
  /*
  if (transfer_head && !quit){
    LOG("--- TRANSFERS ---");
    
    while(transfer_head){
      struct test_transfer *transfer = transfer_head;
      transfer_head = transfer->next;
      if (!transfer_head)
	transfer_tail = NULL;
      
      LOGF("%s - %s, sending %s", 
	transfer->src->name, transfer->dest->name, alloca_sync_key(&transfer->key->key));
      
      sync_add_key(transfer->dest->state, &transfer->key->key, transfer->key);
      
      free(transfer);
    }
    
    goto sync_again;
  }*/
  // now start telling peers that these new keys are arriving
  
  for (i=0;i<peer_count;i++)
    sync_free_state(peers[i].state);
  return quit;
}
