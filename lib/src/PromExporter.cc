#include <drogon/plugins/PromExporter.h>
#include <drogon/HttpAppFramework.h>
#include <drogon/utils/monitoring/Collector.h>

using namespace drogon;
using namespace drogon::monitoring;
using namespace drogon::plugin;

void PromExporter::initAndStart(const Json::Value &config)
{
    path_ = config.get("path", path_).asString();
    LOG_ERROR << path_;
    auto &app = drogon::app();
    std::weak_ptr<PromExporter> weakPtr = shared_from_this();
    app.registerHandler(
        path_,
        [weakPtr](const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback) {
            auto thisPtr = weakPtr.lock();
            if (!thisPtr)
            {
                auto resp = HttpResponse::newNotFoundResponse();
                callback(resp);
                return;
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(thisPtr->exportMetrics());
            resp->setExpiredTime(5);
            callback(resp);
        },
        {Get, Options},
        "PromExporter");
}
static std::string exportCollector(
    const std::shared_ptr<CollectorBase> &collector)
{
    auto sampleGroups = collector->collect();
    std::string res;
    res.append("# HELP ")
        .append(collector->name())
        .append(" ")
        .append(collector->help())
        .append("\r\n");
    res.append("# TYPE ")
        .append(collector->name())
        .append(" ")
        .append(collector->type())
        .append("\r\n");
    for (auto const &sampleGroup : sampleGroups)
    {
        auto const &metricPtr = sampleGroup.metric;
        auto const &samples = sampleGroup.samples;
        for (auto &sample : samples)
        {
            res.append(metricPtr->name());
            if (!sample.exLabels.empty() || !metricPtr->labels().empty())
            {
                res.append("{");
                for (auto const &label : metricPtr->labels())
                {
                    res.append(label.first)
                        .append("=\"")
                        .append(label.second)
                        .append("\",");
                }
                for (auto const &label : sample.exLabels)
                {
                    res.append(label.first)
                        .append("=\"")
                        .append(label.second)
                        .append("\",");
                }
                res.pop_back();
                res.append("}");
            }
            res.append(" ").append(std::to_string(sample.value));
            if (sample.timestamp.microSecondsSinceEpoch() > 0)
            {
                res.append(" ")
                    .append(std::to_string(
                        sample.timestamp.microSecondsSinceEpoch() / 1000))
                    .append("\r\n");
            }
            else
            {
                res.append("\r\n");
            }
        }
    }
    return res;
}
std::string PromExporter::exportMetrics()
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::string result;
    for (auto const &collector : collectors_)
    {
        result.append(exportCollector(collector));
    }
    return result;
}

void PromExporter::registerCollector(
    const std::shared_ptr<drogon::monitoring::CollectorBase> &collector)
{
    std::lock_guard<std::mutex> guard(mutex_);
    collectors_.emplace_back(collector);
}