#include <galay/factory/factory.h>
#include <galay/util/parser.h>
#include <nlohmann/json.hpp>

std::string dns_server_ip;
int dns_server_port;

galay::Task<> DnsEnquire(galay::Task_Base::wptr task)
{
    auto dns_client = galay::Client_Factory::create_dns_client(task.lock()->get_scheduler());
    auto req = std::dynamic_pointer_cast<galay::protocol::Http1_1_Request>(task.lock()->get_req());
    auto resp = std::dynamic_pointer_cast<galay::protocol::Http1_1_Response>(task.lock()->get_resp());
    resp->get_version() = "1.1";
    if(req->get_url_path().compare("/addr") == 0){
        auto j = nlohmann::json::parse(req->get_body());
        galay::protocol::Dns_Request::ptr dns_req = std::make_shared<galay::protocol::Dns_Request>();
        galay::protocol::Dns_Response::ptr dns_resp = std::make_shared<galay::protocol::Dns_Response>();
        auto &header = dns_req->get_header();
        header.m_flags.m_rd = 1;
        header.m_id = 100;
        header.m_questions = 1;
        galay::protocol::Dns_Question question;
        question.m_class = 1;
        if(j.contains("name")){
            auto names = j["name"];
            question.m_type = galay::protocol::DNS_QUERY_A;
            for(auto it = names.begin() ; it != names.end() ; ++it){
                auto name = *it;
                question.m_qname = name;
                dns_req->get_question_queue().push(question);
                auto ret = co_await dns_client->request(dns_req,dns_resp,dns_server_ip,dns_server_port);
            }
            auto& q = dns_resp->get_answer_queue();
            nlohmann::json res;
            while(!q.empty()){
                galay::protocol::Dns_Answer answer = q.front();
                q.pop();
                if(answer.m_type == galay::protocol::DNS_QUERY_A){
                    res[answer.m_aname]["ipv4"].push_back(answer.m_data);
                }
            }
            question.m_type = galay::protocol::DNS_QUERY_AAAA;
            for(auto it = names.begin() ; it != names.end() ; ++it){
                auto name = *it;
                question.m_qname = name;
                dns_req->get_question_queue().push(question);
                auto ret = co_await dns_client->request(dns_req,dns_resp,dns_server_ip,dns_server_port);
            }
            q = dns_resp->get_answer_queue();
            while(!q.empty()){
                galay::protocol::Dns_Answer answer = q.front();
                q.pop();
                if(answer.m_type == galay::protocol::DNS_QUERY_AAAA){
                    res[answer.m_aname]["ipv6"].push_back(answer.m_data);
                }
            }
            resp->get_status() = galay::protocol::Http1_1_Response::OK_200;
            resp->set_head_kv_pair({"Connection","close"});
            resp->get_body() = res.dump();
        }else{
            resp->get_status() = galay::protocol::Http1_1_Response::BadRequest_400;
            resp->set_head_kv_pair({"Connection","close"});
        }
    }else if(req->get_url_path().compare("/cname") == 0){
        auto j = nlohmann::json::parse(req->get_body());
        galay::protocol::Dns_Request::ptr dns_req = std::make_shared<galay::protocol::Dns_Request>();
        galay::protocol::Dns_Response::ptr dns_resp = std::make_shared<galay::protocol::Dns_Response>();
        auto &header = dns_req->get_header();
        header.m_flags.m_rd = 1;
        header.m_id = 100;
        header.m_questions = 1;
        galay::protocol::Dns_Question question;
        question.m_class = 1;
        question.m_type = galay::protocol::DNS_QUERY_CNAME;
        if(j.contains("name")){
            auto names = j["name"];
            for(auto it = names.begin() ; it != names.end() ; ++it){
                auto name = *it;
                question.m_qname = name;
                dns_req->get_question_queue().push(question);
                auto ret = co_await dns_client->request(dns_req,dns_resp,dns_server_ip,dns_server_port);
            }
            auto& q = dns_resp->get_answer_queue();
            nlohmann::json res;
            while(!q.empty()){
                galay::protocol::Dns_Answer answer = q.front();
                q.pop();
                if(answer.m_type == galay::protocol::DNS_QUERY_CNAME){
                    res[answer.m_aname]["cname"].push_back(answer.m_data);
                }
            }
            resp->get_status() = galay::protocol::Http1_1_Response::OK_200;
            resp->set_head_kv_pair({"Connection","close"});
            resp->get_body() = res.dump();
        }else{
            resp->get_status() = galay::protocol::Http1_1_Response::BadRequest_400;
            resp->set_head_kv_pair({"Connection","close"});
        }
    }else resp->get_status() = galay::protocol::Http1_1_Response::NotFound_404;
    std::cout << " response :    " << resp->encode() << '\n';
    task.lock()->control_task_behavior(galay::GY_TASK_WRITE);
    co_return;
}

int main()
{
    galay::Parser::ConfigParser parser;
    parser.parse("../conf/Server.conf");
    dns_server_ip = parser.get_value(std::string("dns_server_ip"));
    dns_server_port = std::stoi(parser.get_value(std::string("dns_server_port")));
    int listen_port = std::stoi(parser.get_value(std::string("listen_port")));
    auto server_config = galay::Config_Factory::create_http_server_config(galay::ENGINE_EPOLL,5);
    auto server = galay::Server_Factory::create_http_server(server_config);
    server->start({{listen_port,DnsEnquire}});
    return 0;
}