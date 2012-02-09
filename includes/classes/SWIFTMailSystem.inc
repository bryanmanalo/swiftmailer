<?php
/**
 * @file
 * The implementation of MailSystemInterface which delegates handling of e-mails
 * to the Swift Mailer library.
 */

class SWIFTMailSystem implements MailSystemInterface {

  /**
   * Formats a message composed by drupal_mail().
   *
   * @see http://api.drupal.org/api/drupal/includes--mail.inc/interface/MailSystemInterface/7
   *
   * @param array $message
   *   A message array holding all relevant details for the message.
   *
   * @return string
   *   The message as it should be sent.
   */
  public function format(array $message) {
    if (!empty($message) && is_array($message)) {

      // Get default mail line endings and merge all lines in the e-mail body
      // separated by the mail line endings.
      $line_endings = variable_get('mail_line_endings', MAIL_LINE_ENDINGS);
      $message['body'] = implode($line_endings, $message['body']);

      // Validate message format.
      if (empty($message['params']['format'])) {
        $message['params']['format'] = variable_get(SWIFTMAILER_VARIABLE_FORMAT, SWIFTMAILER_VARIABLE_FORMAT_DEFAULT);
      }

      // Validate message theming
      if (!isset($message['params']['theme'])) {
        $message['params']['theme'] = ($message['params']['format'] == SWIFTMAILER_FORMAT_HTML) ? true : false;
      }

      // Theme message if theming is enabled and format is set to be HTML.
      if (!empty($message['params']['theme']) && $message['params']['format'] == SWIFTMAILER_FORMAT_HTML) {
        $message['body'] = theme('swiftmailer', $message);
      }

      // Process any images specified by 'image:' which are to be added later
      // in the process. All we do here is to alter the message so that image
      // paths are replaced with cid's. Each image gets added to the array
      // which keeps track of which images to embed in the e-mail.
      $embeddable_images = array();
      preg_match_all('/"image:([^"]+)"/', $message['body'], $embeddable_images);
      for ($i = 0; $i < count($embeddable_images[0]); $i++) {

        $image_id = $embeddable_images[0][$i];
        $image_path = trim($embeddable_images[1][$i]);
        $image_name = basename($image_path);

        if (drupal_substr($image_path, 0, 1) == '/') {
          $image_path = drupal_substr($image_path, 1);
        }

        $image = new stdClass();
        $image->uri = $image_path;
        $image->filename = $image_name;
        $image->filemime = file_get_mimetype($image_path);
        $image->cid = swiftmailer_get_random(10);
        $message['params']['images'][] = $image;
        $message['body'] = preg_replace($image_id, 'cid:' . $image->cid, $message['body']);
      }

      return $message;
    }
  }

  /**
   * Sends a message composed by drupal_mail().
   *
   * @see http://api.drupal.org/api/drupal/includes--mail.inc/interface/MailSystemInterface/7
   *
   * @param array $message
   *   A message array holding all relevant details for the message.
   *
   * @return boolean
   *  TRUE if the message was successfully sent, and otherwise FALSE.
   */
  public function mail(array $message) {

    // Include required file.
    module_load_include('inc', 'swiftmailer', '/includes/helpers/conversion');

    //Validate whether the Swift Mailer module has been configured.
    $library_path = variable_get(SWIFTMAILER_VARIABLE_PATH, SWIFTMAILER_VARIABLE_PATH_DEFAULT);
    if (empty($library_path)) {
      watchdog('swiftmailer', 'An attempt to send an e-mail failed. The Swift Mailer library could not be found by the Swift Mailer module.', array(), WATCHDOG_ERROR);
      drupal_set_message(t('An attempt to send the e-mail failed. The e-mail has not been sent.'), 'error');
      return;
    }

    //Include the Swift Mailer library.
    require_once(drupal_realpath($library_path . '/lib/swift_required.php'));

    try {

      // Create a new message.
      $m = Swift_Message::newInstance();

      // Not all Drupal headers should be added to the e-mail message.
      // Some headers must be supressed in order for Swift Mailer to
      // do its work properly.
      $suppressable_headers = swiftmailer_get_supressable_headers();

      // Process headers provided by Drupal. We want to add all headers which
      // are provided by Drupal to be added to the message. For each header we
      // first have to find out what type of header it is, and then add it to
      // the message as the particular header type.
      if (!empty($message['headers']) && is_array($message['headers'])) {
        foreach ($message['headers'] as $header_key => $header_value) {

          // Check wether the current header key is empty or represents
          // a header that should be supressed. If yes, then skip header.
          if (empty($header_key) || in_array($header_key, $suppressable_headers)) {
            continue;
          }

          // Get header type.
          $header_type = swiftmailer_get_headertype($header_key, $header_value);

          // Add the current header to the e-mail message.
          switch ($header_type) {
            case SWIFTMAILER_HEADER_ID:
              swiftmailer_add_id_header($m, $header_key, $header_value);
              break;
            case SWIFTMAILER_HEADER_PATH:
              swiftmailer_add_path_header($m, $header_key, $header_value);
              break;
            case SWIFTMAILER_HEADER_MAILBOX:
              swiftmailer_add_mailbox_header($m, $header_key, $header_value);
              break;
            case SWIFTMAILER_HEADER_DATE:
              swiftmailer_add_date_header($m, $header_key, $header_value);
              break;
            case SWIFTMAILER_HEADER_PARAMETERIZED:
              swiftmailer_add_parameterized_header($m, $header_key, $header_value);
              break;
            default:
              swiftmailer_add_text_header($m, $header_key, $header_value);
              break;
          }
        }
      }

      // Set basic message details.
      swiftmailer_remove_header($m, 'From');
      swiftmailer_remove_header($m, 'To');
      swiftmailer_remove_header($m, 'Subject');
      $m->setFrom($message['from']);
      $m->setTo($message['to']);
      $m->setSubject($message['subject']);

      // Add content to the message. The default content type will be applied
      // to the message (via setBody()) unless a content type is provided in
      // $message['params']['format'].
      $m->getHeaders()->removeAll('Content-Type');
      if (empty($message['params']['format'])) {
        $m->setBody($message['body'], variable_get(SWIFTMAILER_VARIABLE_FORMAT, SWIFTMAILER_VARIABLE_FORMAT_DEFAULT));
      } else {
        $m->setBody($message['body'], $message['params']['format']);
      }

      // Apply character set to the message. The default character set will
      // be applied to the message unless a charset is provided in
      // $message['params']['charset'].
      if (empty($message['params']['charset'])) {
        $m->setCharset(variable_get(SWIFTMAILER_VARIABLE_CHARACTER_SET, SWIFTMAILER_VARIABLE_CHARACTER_SET_DEFAULT));
      } else {
        $m->setCharset($message['params']['charset']);
      }

      // Validate that $message['params']['files'] is an array.
      if (empty($message['params']['files']) || !is_array($message['params']['files'])) {
        $message['params']['files'] = array();
      }

      // Let other modules get the chance to add attachable files.
      $files = module_invoke_all('swiftmailer_attach', $message['key']);
      if (!empty($files) && is_array($files)) {
        $message['params']['files'] = array_merge(array_values($message['params']['files']), array_values($files));
      }

      // Attach files.
      if (!empty($message['params']['files']) && is_array($message['params']['files'])) {
        $this->attach($m, $message['params']['files']);
      }

      // Embed images.
      if (!empty($message['params']['images']) && is_array($message['params']['images'])) {
        $this->embed($m, $message['params']['images']);
      }

      static $mailer;

      // If required, create a mailer which will be used to send the message.
      //if (empty(static::$mailer)) {
      if (empty($mailer)) {

        //Get the configured transport type.
        $transport_type = variable_get(SWIFTMAILER_VARIABLE_TRANSPORT, SWIFTMAILER_VARIABLE_TRANSPORT_DEFAULT);

        // Configure the mailer based on the configured transport type.
        switch ($transport_type) {
          case SWIFTMAILER_TRANSPORT_SMTP:

          //Get transport configuration.
            $host = variable_get(SWIFTMAILER_VARIABLE_SMTP_HOST, SWIFTMAILER_VARIABLE_SMTP_HOST_DEFAULT);
            $port = variable_get(SWIFTMAILER_VARIABLE_SMTP_PORT, SWIFTMAILER_VARIABLE_SMTP_PORT_DEFAULT);
            $encryption = variable_get(SWIFTMAILER_VARIABLE_SMTP_ENCRYPTION, SWIFTMAILER_VARIABLE_SMTP_ENCRYPTION_DEFAULT);
            $username = variable_get(SWIFTMAILER_VARIABLE_SMTP_USERNAME, SWIFTMAILER_VARIABLE_SMTP_USERNAME_DEFAULT);
            $password = variable_get(SWIFTMAILER_VARIABLE_SMTP_PASSWORD, SWIFTMAILER_VARIABLE_SMTP_PASSWORD_DEFAULT);

            //Instantiate transport.
            $transport = Swift_SmtpTransport::newInstance($host, $port);

            //Set encryption (if any).
            if (!empty($encryption)) {
              $transport->setEncryption($encryption);
            }

            //Set username (if any).
            if (!empty($username)) {
              $transport->setUsername($username);
            }

            //Set password (if any).
            if (!empty($password)) {
              $transport->setPassword($password);
            }

            //static::$mailer = Swift_Mailer::newInstance($transport);
            $mailer = Swift_Mailer::newInstance($transport);
            break;

          case SWIFTMAILER_TRANSPORT_SENDMAIL:

          //Get transport configuration.
            $path = variable_get(SWIFTMAILER_VARIABLE_SENDMAIL_PATH, SWIFTMAILER_VARIABLE_SENDMAIL_PATH_DEFAULT);
            $mode = variable_get(SWIFTMAILER_VARIABLE_SENDMAIL_MODE, SWIFTMAILER_VARIABLE_SENDMAIL_MODE_DEFAULT);

            //Instantiate transport.
            $transport = Swift_SendmailTransport::newInstance($path . ' -' . $mode);
            //static::$mailer = Swift_Mailer::newInstance($transport);
            $mailer = Swift_Mailer::newInstance($transport);
            break;

          case SWIFTMAILER_TRANSPORT_NATIVE:

          //Instantiate transport.
            $transport = Swift_MailTransport::newInstance();
            //static::$mailer = Swift_Mailer::newInstance($transport);
            $mailer = Swift_Mailer::newInstance($transport);
            break;
        }
      }

      // Send the message.
      //return static::$mailer->send($m);
      return $mailer->send($m);

    } catch (Exception $e) {

      // Log error and notify user.
      watchdog('swiftmailer', 'An attempt to send an e-mail message failed: !exception_message', array('!exception_message' => (drupal_substr(trim($e->getMessage()), -1) == '.') ? trim($e->getMessage()) : trim($e->getMessage()) . '.'), WATCHDOG_ERROR);
      drupal_set_message(t('An attempt to send an e-mail message failed.'), 'error');

    }
  }

  private function attach(Swift_Message $m, $files) {

    // Iterate through each array element.
    foreach ($files as $file) {

      if ($file instanceof stdClass) {

        //Validate required fields.
        if (empty($file->uri) || empty($file->filename) || empty($file->filemime)) {
          continue;
        }

        // Get file data.
        $content = file_get_contents(drupal_realpath($file->uri));
        $filename = $file->filename;
        $filemime = $file->filemime;

        // Attach file.
        $m->attach(Swift_Attachment::newInstance($content, $filename, $filemime));
      }
    }

  }

  private function embed(Swift_Message $m, $images) {

    // Iterate through each array element.
    foreach ($images as $image) {

      if ($image instanceof stdClass) {

        // Validate required fields.
        if (empty($image->uri) || empty($image->filename) || empty($image->filemime) || empty($image->cid)) {
          continue;
        }

        // Keep track of the 'cid' assigned to the embedded image.
        $cid = NULL;

        // Get image data.
        $content = file_get_contents(drupal_realpath($image->uri));
        $filename = $image->filename;
        $filemime = $image->filemime;

        // Embed image.
        $cid = $m->embed(Swift_Image::newInstance($content, $filename, $filemime));

        // The provided 'cid' needs to be replaced with the 'cid' returned
        // by the Swift Mailer library.
        $body = $m->getBody();
        $body = preg_replace('/cid.*' . $image->cid . '/', $cid, $body);
        $m->setBody($body);
      }
    }
  }
}