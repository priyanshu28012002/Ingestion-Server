calss GstPipeline {
    public :
    explicit GstPineline(const Camera& Camera)
    :camera_(camera), pipeline_(nullptr) {}

     ~GstPipeline() {
        stop();
    }

    
}