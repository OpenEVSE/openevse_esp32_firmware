#ifndef _OPENEVSE_JSON_SERIALIZE
#define _OPENEVSE_JSON_SERIALIZE

#include <Arduino.h>
#include <ArduinoJson.h>

template <size_t CAPACITY> class JsonSerialize
{
  private:
    static const size_t _capacity = CAPACITY;
  public:
    virtual bool deserialize(const char *json)
    {
      DynamicJsonDocument doc(_capacity);
      DeserializationError err = deserializeJson(doc, json);
      if(DeserializationError::Code::Ok == err) {
        return deserialize(doc);
      }
      return false;
    }

    virtual bool deserialize(String &json) {
      return deserialize(json.c_str());
    }

    virtual bool deserialize(Stream &stream)
    {
      DynamicJsonDocument doc(_capacity);
      DeserializationError err = deserializeJson(doc, stream);
      if(DeserializationError::Code::Ok == err) {
        return deserialize(doc);
      }
      return false;
    }

    virtual bool deserialize(DynamicJsonDocument &doc)
    {
      if (doc.is<JsonObject>())
      {
        JsonObject obj = doc.as<JsonObject>();
        return deserialize(obj);
      }

      return false;
    }

    virtual bool deserialize(JsonObject &obj) = 0;

    virtual bool serialize(String &json)
    {
      DynamicJsonDocument doc(_capacity);
      if(serialize(doc))
      {
        serializeJson(doc, json);
        return true;
      }

      return true;
    }

    virtual bool serialize(Stream *stream)
    {
      if(stream) {
        return serialize(*stream);
      }

      return false;
    }

    virtual bool serialize(Stream &stream)
    {
      DynamicJsonDocument doc(_capacity);
      if(serialize(doc))
      {
        serializeJson(doc, stream);
        return true;
      }

      return true;
    }

    virtual bool serialize(Print *print)
    {
      if(print) {
        return serialize(*print);
      }

      return false;
    }

    virtual bool serialize(Print &print)
    {
      DynamicJsonDocument doc(_capacity);
      if(serialize(doc))
      {
        serializeJson(doc, print);
        return true;
      }

      return true;
    }

    virtual bool serialize(DynamicJsonDocument &doc)
    {
      JsonObject object = doc.to<JsonObject>();
      return serialize(object);
    }

    virtual bool serialize(JsonObject &obj) = 0;
};

#endif // !_OPENEVSE_JSON_SERIALIZE
