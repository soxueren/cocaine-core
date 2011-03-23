#include <string>
#include <vector>
#include <map>
#include <stdexcept>

#include <time.h>
#include <syslog.h>
#include <sys/types.h>
#include <regex.h>

#include <zmq.hpp>

#include "digest.hpp"
#include "registry.hpp"

#define clock_advance(tsa, tsb)      \
    do {                             \
        tsa.tv_sec += tsb.tv_sec;    \
        tsa.tv_nsec += tsb.tv_nsec;  \
        if (tsa.tv_nsec >= 1e+9) {   \
            tsa.tv_sec++;            \
            tsa.tv_nsec -= 1e+9;     \
        }                            \
    } while(0);

#define clock_parse(interval, ts)              \
    do {                                       \
        ts.tv_sec = interval / 1000;           \
        ts.tv_nsec = (interval % 1000) * 1e+6; \
    } while(0);

// Message structure used by the engines to
// pass events back to the core
struct event_t {
    event_t(const std::string& key_):
        key(key_),
        dict(NULL) {}

    std::string key;
    dict_t* dict;
};

// Engine workload
struct workload_t {
    workload_t(const std::string& key_, source_t* source_, zmq::context_t& context_, time_t interval_):
        key(key_),
        source(source_),
        context(context_)
    {
        clock_parse(interval_, interval);
        pthread_spin_init(&datalock, PTHREAD_PROCESS_PRIVATE);
        pthread_mutex_init(&sleeplock, NULL);
        pthread_cond_init(&sleepcond, NULL);
    }

    ~workload_t() {
        pthread_cond_destroy(&sleepcond);
        pthread_mutex_destroy(&sleeplock);
        pthread_spin_destroy(&datalock);
    }

    std::string key;
    source_t* source;
    zmq::context_t& context;
    timespec interval;
    
    // Flow control
    pthread_spinlock_t datalock;
    pthread_mutex_t sleeplock;
    pthread_cond_t sleepcond;
};

// Engine
class engine_t {
    public:
        static void* poll(void *arg);

        engine_t(const std::string& key, source_t* source, zmq::context_t& context,
            time_t interval, time_t ttl);
        ~engine_t();

        void subscribe(time_t interval, time_t ttl);
        void unsubscribe() { if(m_refs) m_refs--; }

        // This also checks whether the engine has expired, and
        // if it is, stops it
        bool reapable(const timespec& now);

    private:
        unsigned int m_refs;
        timespec m_timestamp;
        time_t m_ttl;

    private:
        pthread_t m_thread;
        workload_t m_workload;
};

// Event loop and networking
class core_t {
    public:
        core_t(char* ep_req, char* ep_export, time_t interval,
            int64_t watermark, unsigned int io_threads);
        ~core_t();

        // The event loop
        void run();
        inline void stop() { m_running = false; }
    
    public:
        static const char identity[];
        static const int version[];

    private:
        // Request dispatcher
        void dispatch(const std::string& request);

        // Request handlers
        void subscribe(const std::string& uri, time_t interval, time_t ttl);
        void unsubscribe(const std::string& uri);

        // A helper to respond with a string
        void respond(const std::string& response);

        // Processes and disassemble the events
        void feed(event_t& event);

    private:
        // Key generator
        digest_t m_keygen;

        // Plugin registry
        registry_t m_registry;

        // Engines
        typedef std::map<std::string, engine_t*> engines_t;
        engines_t m_engines;

        // Events pending for publishing
        typedef std::vector<std::string> pending_t;
        pending_t m_pending;

        // Networking
        zmq::context_t m_context;
        zmq::socket_t s_events, s_requests, s_export;

        // Loop control
        timespec m_now, m_interval;
        unsigned int m_watermark;
        bool m_running;
    
        // Command regexps
        regex_t r_subscribe, r_unsubscribe;
};
