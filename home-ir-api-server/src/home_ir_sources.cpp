// Arduino CLI compiles sketch/src recursively, but does not collect arbitrary
// sibling directories. Keep the public architecture in api/, devices/, and
// ir/, then include their implementation units from this build aggregator.
#include "../ir/IrSender.cpp"
#include "../devices/light/CeilingLight.cpp"
#include "../devices/daikin/DaikinAircon.cpp"
#include "../api/LightApi.cpp"
#include "../api/AirconApi.cpp"
#include "../api/SystemApi.cpp"
